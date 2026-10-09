# Geometry-aware Vegas: 독립 재현 패키지

이 폴더만 복사하거나 Git으로 내려받으면 실험을 실행할 수 있다.
이전 `vegas_recent`, `vegas_oracle`, `vegas_diagnostics`, 상위 `runs` 폴더나
기존 `lab1` 컨테이너/소스 볼륨은 필요하지 않다.

필수 실행 환경은 **Linux 컨테이너 모드의 Docker Desktop**이다.
ns-3.48, 컴파일러, Python, NumPy, Matplotlib은 지정된 수업 이미지에 포함되어 있다.
이미지는 `environment.json`의 SHA-256 digest로 고정한다.
처음 이미지를 받는 환경에서는 인터넷과 해당 이미지의 다운로드 권한이 필요하다.
완전한 오프라인 환경에서는 그 이미지를 미리 준비해야 한다.

## PowerShell에서 실행

이 폴더를 PowerShell에서 연 뒤:

```powershell
# 전체 14개 실행 + 그래프/지표 생성 + 과거 결과와 비교
powershell -ExecutionPolicy Bypass -File .\reproduce.ps1
```

짧은 설치·실행 확인만 할 때:

```powershell
powershell -ExecutionPolicy Bypass -File .\reproduce.ps1 -Suite smoke
```

분리해서 실행할 수도 있다. 매 실행은 깨끗한 컨테이너에서 시작하고 소스를 빌드한다.

```powershell
powershell -ExecutionPolicy Bypass -File .\reproduce.ps1 -Suite dynamic -OutputName dynamic-01
powershell -ExecutionPolicy Bypass -File .\reproduce.ps1 -Suite congestion -OutputName congestion-01
powershell -ExecutionPolicy Bypass -File .\reproduce.ps1 -Suite sharing -OutputName sharing-01
```

`congestion`에는 ACK 진단 실행도 포함한다. `all`은 모든 실험과 최종 교차 검증까지 실행한다.
`-Jobs 2`가 기본값이며 메모리가 부족하면 `-Jobs 1`로 실행할 수 있다.
같은 출력 이름은 덮어쓰지 않는다. 실패 후 다시 실행할 때도 새 이름을 사용한다.

실행 순서는 입력 파일 해시 검사 → 최종 소스 설치 → 빌드 → 시뮬레이션 → 분석 → 검증이다.
기존 실험이나 도커 볼륨을 수정하지 않으며 임시 실행 컨테이너는 종료 시 제거한다.

## 출력

`output/<실행 이름>/` 아래에 모두 저장한다.

- `reproduction.json`: 성공/실패, 실행 환경, 전체 소요 시간
- `build.log`: 컴파일 로그
- `runs/vegas_geometry_*/`: 실험 설정, 명령·환경 기록, 원시 로그
- `analysis/*.png`: 성능, 갱신 주기, 전달 지연, 경쟁 부하, ACK 진단 그래프
- `analysis/*_metrics.json`, `*_comparison.csv`: 정량 결과
- `analysis/validation.json`: 전체 실행에서의 교차 검증 결과
- `*_analysis.log`: 분석 실패 시 확인할 로그

원래 실험의 그림과 요약 수치는 이 폴더의 `analysis/`에 보존했다.
재실행 결과는 `output/`에 저장하므로 원래 결과를 덮어쓰지 않는다.

## 포함된 자료

| 경로 | 용도 |
|---|---|
| `source/` | 수정된 ns-3 소스 11개 전체. 이 파일들이 실제 빌드 대상이다 |
| `geometry_from_clean.patch` | 이전 실험 단계가 아닌 깨끗한 지정 이미지 기준 변경 내역 |
| `installed_manifest.json`, `base_manifest.json` | 최종 소스와 원본 소스 해시 |
| `inputs/state.tar.gz` | 실제 사용한 위성 위치·경로 입력 4,007개 |
| `inputs/state_manifest.json` | 압축 해제한 각 입력의 무결성 검증 |
| `inputs/config_ns3.properties`, `schedule.csv` | 실험 설정 템플릿과 TCP 스케줄 |
| `reference/oracle_probe.csv` | 예측 오차를 평가하는 독립 무부하 관측 자료 |
| `reference/control_hashes.json` | 기존 Vegas의 cwnd/RTT/progress 로그 해시 |
| `reference/*_metrics.json` | 기존 실험과 재실행 결과 비교용 지표 |
| `requirements.txt`, `environment.json` | 분석 라이브러리와 실행 환경 버전 |
| `licenses/` | 기반 소스의 원래 라이선스 문서 |
| `experiment_report.txt` | 알고리즘·이전 결과·한계의 자세한 설명 |

`reference`는 분석과 검증에서만 읽는다. 시뮬레이션 제어 알고리즘은 참조 RTT나 큐 정보를 입력받지 않는다.
위성·경로 입력은 이 폴더 안의 `.runtime/state`에 해제하고, 생성되는 설정 파일에 그 상대 경로를 넣는다.
예전 컴퓨터의 절대 경로를 사용할 필요가 없다.

소스의 원본 라이선스 헤더는 보존했다. `source/`는 지정 ns-3/수업 환경의 수정 파일이며
별도의 독자 라이선스를 부여하지 않는다. 기반 이미지와 입력 자료의 배포 조건은 해당 제공자의 조건을 따른다.

## 검증 범위

2026-10-09: 기존 컨테이너/소스 볼륨 없이 이 폴더만 마운트한 깨끗한 이미지에서
전체 14개 실행을 완료했고, 기존 결과의 77개 비교 구간에서 모든 통신 지표가 일치했다.
별도 경로에 복사한 폴더에서도 PowerShell 진입 명령으로 smoke 실행을 통과했다.
검증 기록은 `reproducibility.json`에 보존했다.

- 깨끗한 기반 소스 또는 이미 설치된 동일 소스에만 적용하며, 알 수 없는 로컬 변경은 덮어쓰지 않는다.
- 기능을 끈 200초 실행의 cwnd/RTT/progress 로그가 원래 로그 해시와 일치하는지 검사한다.
- 정보가 패킷 전송 추정 시각 이후에 생성·전달된 경우 사용하지 않았는지 검사한다.
- 기준 RTT 적용식, 캐시 공유, 경쟁 부하에 따른 모델 예측값 불변성을 검사한다.
- 전체 실행은 기존 결과의 모든 비교 구간에서 지표 일치를 검사한다. 계산 시간은 비교 대상에서 제외한다.

계산·조회 시간은 컴퓨터와 부하에 따라 달라진다. 주된 통신 지표는 동일 소스·입력·seed에서 재현을 확인한다.
다른 CPU 아키텍처나 다른 도구 체인까지 비트 단위 재현을 보장하는 것은 아니다.

## 결과 해석

121.6~200초: 기존 Vegas 27.83Mbps, 예측 캐시 100ms 89.13Mbps.
두 경우 단일 큐 최대는 5패킷, 개별 큐 최대 대기는 약 0.459ms였다.
그러나 고정 경로+경쟁 부하의 90~100초에서는 기존 6.18Mbps, 수정 버전 1.55Mbps로 악화됐다.
ACK 설정 변경은 원인 진단용이며 기본 알고리즘의 성능으로 제시하지 않는다.

위치·현재 경로 정보에 접근할 수 있는 모듈을 가정한다. 기본 실험에는 계산에 따른 모의 시간 지연과
실제 정보 전달 트래픽을 넣지 않았다. 별도로 100/500ms 정보 전달 지연만 시험했다.
이 결과는 완성된 실망 배치나 모든 혼잡 상황의 해결을 의미하지 않는다.

## Git에 올리기

이 폴더의 `.gitignore`를 함께 올리면 `output`, `.runtime`, 로컬 백업·캐시·중간 파일은 제외된다.
`source`, `inputs`, `reference`, `analysis`와 실행 스크립트·문서는 모두 포함해야 한다.
아직 Git 저장소를 생성하거나 원격 저장소로 업로드하지는 않았다.
