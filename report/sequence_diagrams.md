# Sequence Diagrams (cho báo cáo & slide thuyết trình)

> Tất cả diagram được viết bằng **Mermaid syntax**. Có thể render trực tiếp bằng VS Code, GitHub, hoặc tại https://mermaid.live.

## 1. Kịch bản 1 — Truyền bình thường (no loss)

```mermaid
sequenceDiagram
    autonumber
    participant A as ESP32_A (Sender)
    participant Br as Mosquitto Broker
    participant B as ESP32_B (Receiver)

    A->>Br: PUBLISH sensor/data {seq:1, temp:28.5}
    Br->>B: deliver {seq:1, temp:28.5}
    Note over B: expected=1, seq==expected → OK
    B->>B: process(temp), LED_GREEN nháy
    B->>Br: PUBLISH sensor/ack {ack_seq:1, status:OK}
    Br->>A: deliver {ack_seq:1}
    Note over A: tìm pending[1], xoá khỏi buffer
    A->>A: LED_GREEN nháy, stat_ack_ok++
```

## 2. Kịch bản 2 — Mất data → retry thành công

```mermaid
sequenceDiagram
    autonumber
    participant A as ESP32_A
    participant Br as Broker
    participant B as ESP32_B

    A->>A: pending[2] = {seq:2, ...}
    A--xBr: PUBLISH seq=2 (DROP-SIM)
    Note over A: không có ACK trong 2s
    Note over A: pending[2].retry_count = 1
    A->>A: LED_YELLOW nháy
    A->>Br: PUBLISH seq=2 (retry 1)
    Br->>B: deliver {seq:2}
    Note over B: seq==expected → OK
    B->>Br: ACK {ack_seq:2, status:OK}
    Br->>A: deliver ACK
    Note over A: xoá pending[2]
```

## 3. Kịch bản 3 — Mất ACK → Duplicate detection

```mermaid
sequenceDiagram
    autonumber
    participant A as ESP32_A
    participant Br as Broker
    participant B as ESP32_B

    A->>Br: PUBLISH seq=3
    Br->>B: deliver {seq:3}
    Note over B: process, expected→4
    B--xBr: ACK seq=3 (ACK-DROP-SIM)
    Note over A: timeout, retry
    A->>Br: PUBLISH seq=3 (retry)
    Br->>B: deliver {seq:3} lần 2
    Note over B: seq=3 < expected=4<br/>→ DUPLICATE, LED_YELLOW
    B->>Br: ACK {ack_seq:3, status:DUPLICATE}
    Br->>A: deliver ACK
    Note over A: xoá pending[3]
```

## 4. Kịch bản 4 — Out-of-order với reorder buffer

```mermaid
sequenceDiagram
    autonumber
    participant A as ESP32_A
    participant Br as Broker
    participant B as ESP32_B

    Note over B: expected = 4
    A--xBr: PUBLISH seq=4 (DROP)
    A->>Br: PUBLISH seq=5
    Br->>B: deliver {seq:5}
    Note over B: seq=5 > expected=4<br/>→ OUT_OF_ORDER, LED_RED<br/>reorder[5] = msg
    B->>Br: ACK {ack_seq:5, status:OUT_OF_ORDER}
    Br->>A: deliver ACK (A xoá pending[5])
    Note over A: pending[4] timeout
    A->>Br: PUBLISH seq=4 (retry)
    Br->>B: deliver {seq:4}
    Note over B: seq==expected=4 → OK<br/>process(seq=4), expected→5
    B->>B: drain_reorder_buffer:<br/>thấy reorder[5] → process(seq=5)<br/>expected→6
    B->>Br: ACK seq=4 status:OK
    B->>Br: ACK seq=5 status:OK (lại)
    Br->>A: deliver ACK
```

## 5. Kịch bản 5 — Fail sau MAX_RETRY

```mermaid
sequenceDiagram
    autonumber
    participant A as ESP32_A
    participant Br as Broker
    participant B as ESP32_B

    A--xBr: seq=6 (drop)
    Note over A: retry 1 (2s)
    A--xBr: seq=6 (drop)
    Note over A: retry 2 (2s)
    A--xBr: seq=6 (drop)
    Note over A: retry 3 (2s)
    A--xBr: seq=6 (drop)
    Note over A: vượt MAX_RETRY → FAILED<br/>LED_RED bật 2s<br/>stat_failed++<br/>xoá pending[6]
    A->>Br: PUBLISH monitor/failed {seq:6}
    Note over A: seq=7 vẫn được gửi tiếp<br/>(không stop pipeline)
```

## 6. Kịch bản 6 — Sliding flow nhiều message song song

```mermaid
sequenceDiagram
    autonumber
    participant A as ESP32_A
    participant Br as Broker
    participant B as ESP32_B

    Note over A: SEND_INTERVAL=2s, RETRY_TIMEOUT=2s
    A->>Br: seq=10
    Br->>B: deliver 10
    B->>Br: ACK 10
    A->>Br: seq=11 (drop)
    A->>Br: seq=12
    Br->>B: deliver 12
    Note over B: OOO, buffer[12]
    B->>Br: ACK 12 (status:OOO)
    Br->>A: ACK 10
    Br->>A: ACK 12
    Note over A: pending[11] vẫn còn
    A->>Br: seq=11 (retry)
    Br->>B: deliver 11
    Note over B: OK → process 11 → drain → process 12
    B->>Br: ACK 11
    B->>Br: ACK 12 (lần 2 — DUPLICATE-BUF)
```

## 7. State diagram bên Receiver (ESP32_B)

```mermaid
stateDiagram-v2
    [*] --> Idle
    Idle --> CheckChecksum: receive msg(seq)
    CheckChecksum --> Corrupt: checksum sai
    Corrupt --> Idle: bỏ, không ACK<br/>(sender sẽ retry)

    CheckChecksum --> CheckSeq: checksum OK

    CheckSeq --> Duplicate: seq < expected<br/>HOẶC seq trong reorder
    Duplicate --> SendAck_Dup: ACK status=DUPLICATE
    SendAck_Dup --> Idle

    CheckSeq --> Process: seq == expected
    Process --> ApplyActuator
    ApplyActuator --> IncExpected: expected++
    IncExpected --> DrainReorder: check reorder[expected]
    DrainReorder --> SendAck_Ok: ACK status=OK
    SendAck_Ok --> Idle

    CheckSeq --> Reorder: seq > expected
    Reorder --> StoreBuffer: reorder[seq] = msg
    StoreBuffer --> SendAck_Ooo: ACK status=OUT_OF_ORDER
    SendAck_Ooo --> Idle
```

## 8. Diagram tổng quan tầng giao thức trong đồ án

```mermaid
flowchart TB
    subgraph ESP32_A [ESP32_A - Sender]
        APP_A[App: đọc DHT11, gắn seq, ACK manager, retry]
        MQTT_A[MQTT QoS 0 PubSubClient]
        APP_A --> MQTT_A
    end

    subgraph ESP32_B [ESP32_B - Receiver]
        APP_B[App: kiểm seq, reorder, ACK, điều khiển LED]
        MQTT_B[MQTT QoS 0 PubSubClient]
        APP_B --> MQTT_B
    end

    subgraph Laptop
        Broker[Mosquitto Broker]
        NodeRED[Node-RED Dashboard]
    end

    MQTT_A <-->|sensor/data, sensor/ack| Broker
    MQTT_B <-->|sensor/data, sensor/ack| Broker
    Broker <-->|monitor/*, control/*| NodeRED

    style APP_A fill:#cfeefc
    style APP_B fill:#cfeefc
    style Broker fill:#ffedb8
```

## 9. Activity diagram — vòng đời 1 message

```mermaid
flowchart TD
    Start([Tick mỗi 2s]) --> Read[Đọc DHT11]
    Read --> ValidDHT{dữ liệu hợp lệ?}
    ValidDHT -- No --> Skip[Bỏ qua chu kỳ]
    ValidDHT -- Yes --> NewSeq[current_seq++]
    NewSeq --> Buffer[Lưu pending buffer]
    Buffer --> DropSim{random < drop_rate?}
    DropSim -- Có --> NotPub[Không publish<br/>chờ timeout → retry]
    DropSim -- Không --> Pub[Publish sensor/data]
    Pub --> Wait[Chờ ACK ≤ 2s]
    NotPub --> Wait
    Wait --> AckIn{ACK đến?}
    AckIn -- Yes --> Done[Xoá khỏi pending<br/>LED_GREEN]
    AckIn -- No (timeout) --> RetryCheck{retry < MAX?}
    RetryCheck -- Yes --> Inc[retry_count++<br/>LED_YELLOW]
    Inc --> Pub
    RetryCheck -- No --> Failed[LED_RED, stat_failed++<br/>xoá khỏi buffer]
    Done --> End([Hết])
    Failed --> End
    Skip --> End
```
