// KCD2PlayerSubtitles
//
// Shows the player's DBReV-voiced line in the vanilla DialogueMenu subtitle field.
// Scene Director then moves that field into its black bar (bSubtitlesInBar), so the
// two work together without sharing any hooks.
//
// The DBReV message layout below was read from KCD2DialogueCamera.dll's listener by
// disassembly. It is not an official header and has NOT been run against a live game,
// so every read is bounds-checked and the first messages are logged for verification.

#include <atomic>
#include <chrono>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>

namespace
{
	// SKSE message types DBReV sends to listeners registered under the name "DBReV".
	constexpr std::uint32_t kMsgLineStart = 1;
	constexpr std::uint32_t kMsgLineEnd = 2;

	// Offsets inside the start-message payload (observed).
	constexpr std::size_t kStartMinSize = 0x38;
	constexpr std::size_t kOffVersion = 0x00;     // u32, must be >= 1
	constexpr std::size_t kOffAudioSecs = 0x08;   // float
	constexpr std::size_t kOffTotalSecs = 0x0C;   // float
	constexpr std::size_t kOffTopicIndex = 0x10;  // u32, position of the chosen topic

	constexpr std::size_t kEndMinSize = 0x10;

	constexpr const char* kShowFn = "_root.DialogueMenu_mc.ShowDialogueText";
	constexpr const char* kHideFn = "_root.DialogueMenu_mc.HideDialogueText";
	constexpr const char* kTextVar = "_root.DialogueMenu_mc.SubtitleText.text";

	std::atomic<std::uint64_t> g_generation{ 0 };
	std::mutex                 g_mutex;
	std::string                g_shownText;
	int                        g_verboseBudget = 6;  // log raw payloads for the first few messages

	template <class T>
	T ReadAt(const void* a_base, std::size_t a_off)
	{
		T v{};
		std::memcpy(&v, static_cast<const std::byte*>(a_base) + a_off, sizeof(T));
		return v;
	}

	void DumpPayload(const char* a_what, const void* a_data, std::uint32_t a_len)
	{
		if (g_verboseBudget <= 0) {
			return;
		}
		--g_verboseBudget;
		std::string hex;
		const auto  n = (std::min)(a_len, 0x40u);
		char        buf[4];
		for (std::uint32_t i = 0; i < n; ++i) {
			std::snprintf(buf, sizeof(buf), "%02X ", ReadAt<std::uint8_t>(a_data, i));
			hex += buf;
		}
		logs::info("DBReV {} payload ({} bytes): {}", a_what, a_len, hex);
	}

	// Same lookup order as the original: the topic list by index, then the last selected topic.
	// Must run on the main thread.
	std::string ResolveText(std::uint32_t a_topicIndex)
	{
		auto* mgr = RE::MenuTopicManager::GetSingleton();
		if (!mgr) {
			return {};
		}

		if (mgr->dialogueList) {
			std::uint32_t i = 0;
			for (auto* d : *mgr->dialogueList) {
				if (i++ == a_topicIndex) {
					if (d && d->topicText.c_str() && d->topicText.c_str()[0]) {
						logs::info("Player subtitle text from dialogueList (topic {})", a_topicIndex);
						return d->topicText.c_str();
					}
					break;
				}
			}
		}

		if (auto* last = mgr->lastSelectedDialogue; last && last->topicText.c_str() && last->topicText.c_str()[0]) {
			logs::info("Player subtitle text from lastSelectedDialogue (topic {})", a_topicIndex);
			return last->topicText.c_str();
		}

		logs::warn("No display text for DBReV topic index {}", a_topicIndex);
		return {};
	}

	RE::GPtr<RE::GFxMovieView> GetDialogueMovie()
	{
		auto* ui = RE::UI::GetSingleton();
		if (!ui || !ui->IsMenuOpen(RE::DialogueMenu::MENU_NAME)) {
			return nullptr;
		}
		return ui->GetMovieView(RE::DialogueMenu::MENU_NAME);
	}

	void ShowOnMainThread(std::uint64_t a_gen, std::uint32_t a_topicIndex)
	{
		if (a_gen != g_generation.load()) {
			return;  // a newer line already replaced this one
		}

		const auto text = ResolveText(a_topicIndex);
		if (text.empty()) {
			return;
		}

		auto movie = GetDialogueMovie();
		if (!movie) {
			logs::warn("DialogueMenu unavailable; player subtitle was not displayed");
			return;
		}

		RE::GFxValue arg;
		arg.SetString(text);
		movie->InvokeNoReturn(kShowFn, &arg, 1);

		{
			std::scoped_lock lock(g_mutex);
			g_shownText = text;
		}
		logs::info("Player subtitle displayed through DialogueMenu");
	}

	void HideOnMainThread(std::uint64_t a_gen)
	{
		if (a_gen != g_generation.load()) {
			return;
		}

		auto movie = GetDialogueMovie();
		if (!movie) {
			return;
		}

		std::string expected;
		{
			std::scoped_lock lock(g_mutex);
			expected = g_shownText;
		}
		if (expected.empty()) {
			return;
		}

		// Do not wipe the field if something else (an NPC line) has taken it over since.
		RE::GFxValue current;
		if (movie->GetVariable(&current, kTextVar) && current.IsString()) {
			if (expected != current.GetString()) {
				logs::info("Player subtitle hide skipped because DialogueMenu text has changed");
				return;
			}
		} else {
			logs::warn("Could not resolve DialogueMenu's subtitle text field; safe hide skipped");
			return;
		}

		movie->InvokeNoReturn(kHideFn, nullptr, 0);
		{
			std::scoped_lock lock(g_mutex);
			g_shownText.clear();
		}
	}

	void QueueHide(std::uint64_t a_gen)
	{
		if (auto* task = SKSE::GetTaskInterface()) {
			task->AddUITask([a_gen] { HideOnMainThread(a_gen); });
		}
	}

	void OnLineStart(const void* a_data, std::uint32_t a_len)
	{
		DumpPayload("start", a_data, a_len);

		if (!a_data || a_len < kStartMinSize) {
			logs::warn("DBReV line-start payload too small ({} bytes); ignored", a_len);
			return;
		}
		if (ReadAt<std::uint32_t>(a_data, kOffVersion) < 1) {
			logs::warn("DBReV line-start payload has an unexpected version; ignored");
			return;
		}

		const auto audio = ReadAt<float>(a_data, kOffAudioSecs);
		const auto total = ReadAt<float>(a_data, kOffTotalSecs);
		const auto index = ReadAt<std::uint32_t>(a_data, kOffTopicIndex);

		float hold = (std::max)(audio, total);
		if (!(hold >= 0.5f)) {
			hold = 3.0f;  // NaN / tiny / unmeasured
		}
		hold = (std::min)(hold, 60.0f);

		const auto gen = ++g_generation;
		logs::info("DBReV player line start: topic {}, hold {:.3f}s", index, hold);

		if (auto* task = SKSE::GetTaskInterface()) {
			task->AddUITask([gen, index] { ShowOnMainThread(gen, index); });
		}

		// Fallback timer in case the end message never arrives.
		std::thread([gen, hold] {
			std::this_thread::sleep_for(std::chrono::duration<float>(hold + 0.25f));
			QueueHide(gen);
		}).detach();
	}

	void OnLineEnd(const void* a_data, std::uint32_t a_len)
	{
		DumpPayload("end", a_data, a_len);

		if (!a_data || a_len < kEndMinSize) {
			return;
		}
		logs::info("DBReV player line end");
		QueueHide(g_generation.load());
	}

	void OnDBReVMessage(SKSE::MessagingInterface::Message* a_msg)
	{
		if (!a_msg) {
			return;
		}
		switch (a_msg->type) {
		case kMsgLineStart:
			OnLineStart(a_msg->data, a_msg->dataLen);
			break;
		case kMsgLineEnd:
			OnLineEnd(a_msg->data, a_msg->dataLen);
			break;
		default:
			break;
		}
	}
}

SKSE_PLUGIN_LOAD(const SKSE::LoadInterface* a_skse)
{
	SKSE::Init(a_skse);

	const auto* messaging = SKSE::GetMessagingInterface();
	if (!messaging || !messaging->RegisterListener("DBReV", OnDBReVMessage)) {
		logs::warn("SKSE messaging unavailable; DBReV integration disabled.");
		return true;
	}

	logs::info("KCD2PlayerSubtitles loaded; listening for DBReV.");
	return true;
}
