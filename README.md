# Atmos Bridge 7.1.4ch

업믹서가 만든 **12채널(7.1.4)** 신호를 Windows 공간 음향 API(**ISpatialAudioClient**)로 넘겨,
Windows의 **Dolby Atmos for home theater** 인코더가 **Dolby MAT**로 HDMI → TV eARC → AV 리시버에 보내도록 하는 **VST3 플러그인**입니다.
HDMI LPCM의 8채널 한계를 넘어 7.1.4 12채널을 리시버가 Dolby Atmos로 받습니다.

```
음악/영상 플레이어 → CABLE In 16ch (VB-Audio) → MultiChannel Plugin Host (루프백 입력)
  → zSonic Upmixer (12ch) → [Atmos Bridge 7.1.4ch]
  → Windows 공간 음향 (고정 객체 12개, 7.1.4) → Dolby Atmos for home theater 인코딩 (MAT)
  → HDMI → TV eARC → Denon AVR-X3800H (Dirac Live ART) → 12개 스피커
```

> Windows 10/11 x64 전용 · 48 kHz 기준 · 개인 사용 목적

---

## 1. 다운로드와 설치

1. 이 저장소의 **Releases**에서 `Atmos-Bridge-7.1.4ch-VST3-win64.zip`을 내려받습니다.
2. 압축을 풀고 `Atmos Bridge 7.1.4ch.vst3` **폴더 전체**를 `C:\Program Files\Common Files\VST3`에 복사합니다.
3. 호스트에서 **Rescan**을 눌러 플러그인 목록을 새로 고칩니다.

## 2. Windows 설정 (처음 한 번)

| 장치 | 설정 |
|---|---|
| **AVR_x3800h (AMD High Definition Audio Device)** | 속성 → 고급: 기본 형식 **48000 Hz** · 공간 음향: **Dolby Atmos for home theater** (Dolby Access 필요) |
| **CABLE In 16ch (VB-Audio)** | 기본 형식 **48000 Hz** · 음악/영상 플레이어의 출력 장치(또는 Windows 기본 재생 장치)로 지정 |

AV 리시버는 스피커 구성 7.1.4와 Dirac Live ART 보정을 그대로 둡니다. 보정은 리시버가 Atmos를 디코딩한 뒤 적용되므로 그대로 유지됩니다.

## 3. 호스트(MultiChannel Plugin Host) 설정

| 항목 | 설정 |
|---|---|
| Input | CABLE In 16ch (VB-Audio Virtual Cable) [Loopback] |
| Output | AVR_x3800h (AMD High Definition Audio Device) (2) |
| Sample Rate | 48000 Hz |
| **Exclusive** | **해제 (필수)** — 독점 모드에서는 공간 음향 스트림이 열리지 않습니다 |
| 플러그인 체인 | zSonic Upmixer → (zAuro Pro, 선택) → **Atmos Bridge 7.1.4ch (맨 끝)** |
| Open Auro-3D Encoder | **체인에서 제거** (Atmos 경로에는 필요 없음, 약 1.1초 지연의 원인) |

- 플러그인은 호스트 쪽 출력으로 **무음**을 돌려줍니다. 소리는 Windows 공간 음향으로만 나가므로 두 번 들리지 않습니다.
  호스트 출력(AVR, 공유 모드)은 **클럭 기준** 역할을 하며, 이렇게 해야 호스트와 Atmos 출력이 같은 클럭으로 돌아 드리프트가 생기지 않습니다.
- 플러그인 상단의 출력 장치는 **자동**이면 공간 음향이 켜진 장치(AVR)를 스스로 찾습니다. 확실히 하려면 목록에서 AVR을 직접 고르세요.
- 플러그인은 한 번에 **하나의 인스턴스**만 출력합니다(두 번째 인스턴스는 비활성으로 표시).

## 4. 처음 켰을 때 확인 순서

1. 플러그인 창 상단 상태가 **초록색 "출력 중 · AVR_x3800h…"**인지 확인합니다.
2. 음악을 재생하고 AVR 화면(입력 신호 정보)에 **Dolby Atmos**가 표시되는지 확인합니다.
3. 아래쪽 **순차 테스트 톤**을 눌러 12개 위치가 2초씩 차례로 울리는지, 화면에서 빛나는 스피커와 실제 스피커가 같은지 확인합니다.
4. 채널 표의 **노이즈 버튼(파형 아이콘)**으로 Ch 1~12가 각각 의도한 위치로 나가는지 확인합니다.
5. 호스트 프로젝트를 저장했다가 다시 열어 매핑·뮤트·모드가 복원되는지 확인합니다.

## 5. 화면과 기능

**상단** — 출력 장치 선택(자동/장치 목록) · 상태(출력 중/연결 중/공간 음향 꺼짐/오류, 마우스를 올리면 자세히) · 출력 샘플레이트 · 모드(음악/저지연/초저지연)

**왼쪽: 7.1.4 스피커 배치** — 각 스피커 테두리가 레벨에 따라 **초록 네온**으로 빛나고(천장 4개는 빛줄기도 함께), 클리핑에 가까우면 주황색으로 바뀝니다.
스피커 안의 숫자는 그 위치로 들어가는 **입력 채널 번호**, 아래는 위치 이름입니다. 스피커를 **클릭**하면 그 위치로 핑크 노이즈가 나갑니다.

**오른쪽: 채널 매핑**

| 항목 | 기능 |
|---|---|
| 입력 | Ch 1~12 = 호스트 채널 1~12. 기본 순서 **L, R, C, LFE, SBL, SBR, SL, SR, TFL, TFR, TRL, TRR** |
| 번호 | 출력 위치 번호 (1=L 2=R 3=C 4=LFE 5=SBL 6=SBR 7=SL 8=SR 9=TFL 10=TFR 11=TRL 12=TRR, **0=끄기**). 같은 번호를 두 번 넣으면 경고 후 합산 |
| 출력 위치 | 지정된 위치 이름 (L, R, C …) |
| 노이즈 | 그 입력 채널로 핑크 노이즈 (매핑·뮤트가 그대로 적용됨) |
| 뮤트 | 채널별 **M** · 위쪽 **All Mute / All Unmute** · **기본값**(번호를 1~12로 되돌림) |
| 레벨 | 입력 레벨 미터와 dB |

**아래** — 순차 테스트 톤 / 테스트 정지 · 현재 테스트 경로 · 모드 · 여유 버퍼 · 플러그인 지연(실측) · 끊김 횟수(클릭하면 0으로) · 클럭 동기 상태

### 핑크 노이즈
- **-20 dBFS RMS 고정**. LFE는 **120 Hz 이하**로 제한된 노이즈입니다.
- 미리 생성해 둔 노이즈(4초 반복)를 재생하므로 실시간 계산 부담이 없습니다.
- 테스트 중에는 음악이 잠시 멈추고, 한 번에 한 곳만 나갑니다. AVR 볼륨을 평소보다 조금 낮추고 시작하세요.

### 모드

| 모드 | 여유 버퍼 | 끊김이 생기면 | 플러그인 지연(예상, 호스트 블록 480 기준) | 용도 |
|---|---|---|---|---|
| 음악 | 100 ms | 거의 생기지 않음 | 약 135 ms | 음악 감상 (기본) |
| 저지연 | 20 ms | 10 ms씩 자동 증가 (최대 +60 ms) | 약 55 ms | 일반 영상 |
| 초저지연 | 3 ms | 자동 보정 없음, 횟수만 표시 | 약 38 ms | 립싱크가 중요한 영상 |

- 플러그인 지연에는 Windows 공간 음향의 처리 단위(10 ms)와 호스트 블록 하나가 항상 포함되므로 0이 될 수 없습니다. 호스트 블록이 1024면 약 17 ms가 더해집니다.
- 출력 스레드는 Windows **MMCSS "Pro Audio"** 우선순위로 동작하고, 실시간 버퍼는 **물리 메모리에 고정(VirtualLock)**하며, 오디오 처리 중에는 메모리 할당·잠금이 없습니다.
- Windows 인코딩 → HDMI → TV → AVR 디코딩·Dirac 구간의 지연은 고정이며 플러그인이 줄이거나 측정할 수 없습니다. 영상은 플레이어의 오디오 싱크 설정으로 맞추세요.
- 초저지연에서 끊김이 생기면: Windows 전원 관리 "고성능", AMD 칩셋 드라이버 최신화, LatencyMon으로 드라이버 지연 점검.

## 6. 알아 둘 점

- Dolby 인코딩을 거치므로 원본과 비트 단위로 같지 않습니다(7.1.4 구성에서는 1:1에 가깝습니다).
- 재생 시작·정지 때 리시버가 Atmos ↔ PCM으로 바뀌며 앞부분이 살짝 잘릴 수 있습니다.
- 오프라인 렌더(파일로 내보내기) 중에는 HDMI로 출력하지 않습니다.
- 로그: `다운로드\AtmosBridge\Logs\atmosbridge.log` (문제가 있으면 증상과 함께 알려 주세요)

## 7. 소스에서 빌드

필요: Windows 10/11, Visual Studio 2022 이상(C++ 데스크톱 개발), CMake 3.22+, Git.

```bat
cmake -S . -B build -A x64
cmake --build build --config Release
```

결과물: `build\AtmosBridge_artefacts\Release\VST3\Atmos Bridge 7.1.4ch.vst3` (JUCE 8.0.15는 CMake가 자동으로 내려받습니다)

DSP 단위 테스트: `g++ -std=c++17 -O2 -I Source tests/test_dsp.cpp -o test_dsp && ./test_dsp`

`main`에 push하면 GitHub Actions가 Windows에서 빌드하고, `CMakeLists.txt`의 버전으로 Release를 만들거나 갱신합니다.

## 라이선스 / 고지

- 개인 사용 목적의 프로젝트입니다. JUCE는 AGPLv3 또는 JUCE 상용 라이선스로 배포되므로, 배포 시 해당 조건을 확인하세요.
- Dolby, Dolby Atmos는 Dolby Laboratories의 상표입니다. 이 프로젝트는 Dolby와 관련이 없습니다.
