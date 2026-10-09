## ISL packet loss 설정 방법

시뮬레이션을 실행할 폴더(예: `/work/runs/r3`)의
`config_ns3.properties` 파일을 엽니다. 파일 맨 아래에 다음 줄을 추가합니다.

```properties```
isl_packet_loss_probability=0.001

확률은 0.0부터 1.0까지 설정할 수 있습니다.
0.001은 패킷당 0.1% 손실 확률이며, 0.0은 손실을 비활성화합니다.
이 설정은 ISL에만 적용됩니다.

설정을 저장한 뒤 터미널에서 시뮬레이션을 실행합니다.

run_sim.sh /work/runs/r3