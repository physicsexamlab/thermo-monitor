# 温度・圧力モニター (thermo-monitor)

シリンジを使った熱力学実験のための、**温度と気圧の時系列測定システム**です。
センサー装置（XIAO ESP32C3 + BME280）が 20 Hz で測った値を BLE でブラウザに送り、
ブラウザ側でグラフ表示・CSV 書き出し・画像書き出しを行います。

- `thermo-monitor.html` — ブラウザアプリ「温度・圧力モニター」（インストール不要、単一ファイル）
- `firmware/BME280_BLE_XIAO/` — XIAO ESP32C3 用ファームウェア（BLE + OLED）

## アプリの使い方

**Chrome または Edge** で開きます（Web Bluetooth を使うため、Safari と iOS では動きません）。

1. **Connect Sensor** — 装置を選んで接続する
2. **Record** — 記録を開始する。上に絶対温度 [K]、下に気圧 [hPa] のグラフが伸びる
3. **Stop** — 記録を止める
4. **CSV Export** / **画像保存** — データと画面を保存する

**記録間隔**は 0.05 s（生データ）から 60 s まで選べます。ファームウェアは常に 20 Hz で
送ってくるので、たとえば 1 s を選べば 20 点の平均が 1 点になり、ノイズは 1/√20 に減ります。
ストロークそのものや気体の緩和（τ_gas ≈ 1〜2 s）を見たいときは 0.05〜0.1 s を選びます。

絶対温度は摂氏温度 + 273.15 としてブラウザ側で計算しています
（変換の定義を 1 か所に持たせるため、装置側は K を送りません）。

書き出したファイル名と画像の見出しには、**BLE パケットに載っている装置 ID** が入ります。
OS が報告するデバイス名は使いません（後述）。

## 装置

**ボード**: Seeed Studio XIAO ESP32C3 ／ **センサー**: BME280 ／ **表示**: SSD1306 OLED

| 信号 | BME280 / OLED | XIAO ESP32C3 |
|---|---|---|
| SDA | BME280 SDA + OLED SDA | D4 (GPIO6) |
| SCL | BME280 SCL + OLED SCL | D5 (GPIO7) |
| BME280 電源 | VIN | **3V3**（★5V ではない） |
| BME280 GND | GND | GND |
| OLED 電源 | VCC | 3V3 |
| OLED GND | GND | GND |
| 電源 | — | USB-C 5V |

> **★ BME280 を 5V に繋いではいけません。** モジュール上のレベルシフターにより I2C バスが
> 5V レベルになり、5V トレラントでない ESP32-C3 の GPIO を破壊します。
> **9V 006P も使用不可**です（XIAO の電源電圧上限は 5V）。

## ファームウェアの書き込み

Arduino IDE で `firmware/BME280_BLE_XIAO/BME280_BLE_XIAO.ino` を開きます。

- ボードマネージャ: `esp32 by Espressif Systems`
- ボード: `XIAO_ESP32C3`
- ライブラリ: Adafruit BME280 Library / Adafruit SSD1306 / Adafruit GFX
  （BLE は ESP32 Arduino core 同梱の "ESP32 BLE Arduino" を使うので追加導入は不要）

## BLE パケット形式

Service `0x181A` (Environmental Sensing) / Characteristic `0x2A58` (Analog, NOTIFY)、
デバイス名 `PEL-BME280-XXXXXX`。1 パケット 20 バイト。

| offset | size | type | field |
|---|---|---|---|
| 0 | 4 | float32 LE | `t_s` 起動からの経過秒 |
| 4 | 4 | float32 LE | `temp_c` 摂氏温度 [degC] |
| 8 | 4 | float32 LE | `press_hpa` 気圧 [hPa] |
| 12 | 4 | uint32 LE | `seq` 通し番号（取りこぼし検出用） |
| 16 | 4 | uint32 LE | `dev_id` 装置 ID（MAC 下位 3 バイト） |

**装置 ID をパケットに載せている理由**: BLE のデバイス名は当てになりません。macOS は一度見た
ペリフェラルの名前を OS 側でキャッシュし、Bluetooth を off/on しても古い名前を返し続けることが
あります。またアドバタイズパケットは 31 バイトしかなく、長い名前は切り詰められます。装置の
取り違えを防ぐのが目的なのに名前が信用できないので、装置 ID はデータストリームそのものに載せ、
ブラウザはこれを正としています。

**時刻は装置側で付けています**。`t_s` は「変換区間の中点」の時刻で、ブラウザの受信時刻では
ありません。BLE の遅延・まとめ送り・OLED 描画による取りこぼしがあっても時定数のフィットが
歪まないようにするためです。解析では等間隔を仮定せず、時刻列をそのまま使ってください。

## 測定上の注意

- **レートは常に 20 Hz 固定**。測定の前後でレートを切り替えると、τ ≈ 150 s の立ち上がりランプが
  生じ、2 K 級の偽信号になります。「測る直前だけ高レート」というバースト運転は禁物です。
- **測定前に 10 分間通電**して自己発熱を飽和させてください。20 Hz では自己発熱が約 2.8 K
  あります。一定オフセットである限り差分を取る運用では消えますが、変化すると効きます。
- **30 分を超える連続測定では、BME280 を XIAO から 20〜30 cm 離して**ケーブルで引き出して
  ください。XIAO の熱が乗ると測定値が時間とともに上振れします。I2C は 100 kHz ならこの距離で
  問題なく通ります。
- BME280 の温度は素子自身（ダイ）の温度で、時定数は τ ≈ 150 s あります。一方シリンジ内の気体が
  壁に熱を渡す時定数は τ_gas ≈ 2 s です。**速い現象の「真の温度」は圧力チャネルから読みます**。
  定容にすれば T_gas(t) = T_room · P(t) / P_eq が厳密に成り立ちます。

より詳しい設計の根拠（20 Hz を選んだ理由、データシートの該当箇所など）は
`firmware/BME280_BLE_XIAO/BME280_BLE_XIAO.ino` 冒頭のコメントに書いてあります。

## ライセンス

Copyright © 2026 一般社団法人 国際物理オリンピック2023記念協会
[CC BY-NC 4.0](https://creativecommons.org/licenses/by-nc/4.0/) — 非営利であれば自由に使えます。

---

**English summary**: A browser app (Web Bluetooth) and ESP32-C3 firmware for logging gas
temperature and pressure at 20 Hz in syringe-based thermodynamics experiments for schools.
Open `thermo-monitor.html` in Chrome or Edge, connect, and record. CC BY-NC 4.0.
