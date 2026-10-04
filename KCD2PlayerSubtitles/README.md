# KCD2PlayerSubtitles

KCD2DialogueCamera.dll의 플레이어 자막(DBReV) 기능만 따로 뽑은 소형 SKSE 플러그인입니다.
Scene Director와 훅을 공유하지 않으므로 같이 쓸 수 있습니다.

## 동작
1. DBReV가 보내는 SKSE 메시지(발신자 "DBReV", 타입 1=대사 시작, 2=대사 끝)를 받습니다.
2. 시작 메시지의 topic index로 MenuTopicManager의 대사 목록에서 텍스트를 찾고(없으면 마지막 선택 대사), 대화창의 자막 칸(`DialogueMenu_mc.ShowDialogueText`)에 표시합니다.
3. 끝 메시지, 또는 대사 길이 + 0.25초 뒤에 자막을 지웁니다. 그 사이 NPC 대사가 칸을 가져갔으면 건드리지 않습니다.

Scene Director의 `bSubtitlesInBar=1`이면 이 칸이 검은 바 안으로 옮겨져 표시됩니다.

## 빌드 (Visual Studio 불필요)
1. GitHub에서 새 저장소를 만들고 이 폴더의 파일을 전부 올립니다 (`.github` 폴더 포함).
2. 저장소의 Actions 탭에서 build 워크플로가 끝날 때까지 기다립니다.
3. 완료된 실행의 Artifacts에서 `KCD2PlayerSubtitles`를 받아 `SKSE/Plugins/KCD2PlayerSubtitles.dll`을 모드 폴더에 넣습니다.

## 주의
- 이 코드는 게임에서 실행해 검증하지 못했습니다. DBReV 메시지 구조는 KCD2DialogueCamera.dll을 디스어셈블해서 읽은 값입니다.
- 처음 몇 개의 DBReV 메시지는 원본 바이트를 로그에 남깁니다 (`Documents/My Games/Skyrim Special Edition/SKSE/KCD2PlayerSubtitles.log`). 자막이 안 뜨면 이 로그를 보내 주세요.
- KCD2DialogueCamera.dll과 동시에 켜면 같은 자막 칸을 두 번 쓰므로, 이식이 목적이면 KCD2 쪽은 빼세요.
- 원본의 코드 페이지 정리(sanitizer), 글꼴/색 설정은 포함하지 않았습니다.
