-- CommonLibSSE-NG (multi-runtime fork: SE / AE / VR) is cloned into lib/commonlibsse-ng by the workflow.
includes("lib/commonlibsse-ng")

set_project("KCD2PlayerSubtitles")
set_version("0.1.0")
set_languages("c++23")
set_warnings("allextra")

add_rules("mode.debug", "mode.releasedbg")

target("KCD2PlayerSubtitles")
    add_rules("commonlibsse-ng.plugin", {
        name = "KCD2PlayerSubtitles",
        author = "personal port",
        description = "DBReV player subtitles in the vanilla DialogueMenu subtitle field (for Scene Director)"
    })

    add_files("src/**.cpp")
    add_headerfiles("src/**.h")
    add_includedirs("src")
    set_pcxxheader("src/pch.h")
