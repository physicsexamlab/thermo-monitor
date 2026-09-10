/*
  BME280_BLE_XIAO - XIAO ESP32C3 ファームウェア（BLE + OLED）

  BME280 から温度と気圧を 20 Hz で読み、
    - SSD1306 OLED に 摂氏温度 [C] / 絶対温度 [K] / 気圧 [hPa] を表示
    - 同じ値を BLE notify でブラウザ（thermo-monitor.html）へ送信
  する。OLED 表示は BME280_display_XIAO.ino と同一。

  Board:   Seeed Studio XIAO ESP32C3（秋月電子 117454）
  Library: Adafruit BME280 Library / Adafruit SSD1306 / Adafruit GFX
           BLE は ESP32 Arduino core 同梱の "ESP32 BLE Arduino" を使う（追加導入不要）

  ---------------------------------------------------------------------------
  Arduino IDE の設定
    ボードマネージャ : "esp32 by Espressif Systems"
    ボード           : XIAO_ESP32C3
    ポート           : /dev/cu.usbmodem*
  ---------------------------------------------------------------------------
  配線（BME280_display_XIAO.ino と同一）

    信号          BME280 / OLED           XIAO ESP32C3
    ------------------------------------------------------
    SDA           BME280 SDA + OLED SDA   D4  (GPIO6)
    SCL           BME280 SCL + OLED SCL   D5  (GPIO7)
    BME280 電源   VIN                     3V3   ★5V ではない
    BME280 GND    GND                     GND
    OLED 電源     VCC                     3V3
    OLED GND      GND                     GND
    電源                                  USB-C 5V（単三 4 本の USB バッテリー等）

  ★ BME280 を 5V に繋いではいけない。モジュール上のレベルシフターにより
     I2C バスが 5V レベルになり、5V トレラントでない ESP32-C3 の GPIO を壊す。
  ★ 9V 006P は使用不可（XIAO の電源電圧上限は 5V）。

  ---------------------------------------------------------------------------
  ■ 発熱についての注意（長時間測定では重要）

  BLE を動かすと XIAO は発熱する。BME280 の温度読み値はデータシート p.10
  脚注 9 のとおり「PCB 温度・素子の自己発熱・周囲温度に依存し、通常は周囲
  温度より高い」ので、XIAO の熱が乗ると測定値が時間とともに上振れする。
  30 分の連続測定では、BME280 を XIAO から 20〜30 cm 離してケーブルで
  引き出すこと。I2C は 100 kHz ならこの距離で問題なく通る。

  ---------------------------------------------------------------------------
  ■ BLE パケット形式（thermo-monitor.html と一致させること）

    offset  size  type         field
    ------  ----  -----------  --------------------------------------
    0       4     float32 LE   t_s       ESP32 起動からの経過秒
    4       4     float32 LE   temp_c    摂氏温度 [degC]
    8       4     float32 LE   press_hpa 気圧 [hPa]
    12      4     uint32  LE   seq       通し番号（取りこぼし検出用）
    16      4     uint32  LE   dev_id    装置ID（MAC 下位 3 バイト。上位 1 バイトは 0）
    合計 20 バイト

  dev_id をパケットに載せる理由:
    BLE のデバイス名は当てにならない。(a) macOS は一度見たペリフェラルの名前を
    OS 側でキャッシュし、Bluetooth を off/on しても古い名前を返し続けることがある。
    (b) アドバタイズパケットは 31 バイトしかなく、長い名前は切り詰められる。
    装置の取り違えを防ぐのが目的なのに名前が信用できないので、装置IDは
    データストリームそのものに載せ、ブラウザはこれを正としてファイル名や
    書き出し画像の見出しに使う。OS のキャッシュを一切経由しない。

  絶対温度 K は temp_c + 273.15 としてブラウザ側で計算する
  （変換の定義を 1 か所に持たせるため、K は送らない）。

  Service        : Environmental Sensing (0x181A)
  Characteristic : Analog (0x2A58), NOTIFY
  Device name    : PEL-BME280-XXXXXX （XXXXXX = MAC 下位 3 バイト。個体ごとに異なる）
  ---------------------------------------------------------------------------
  ■ なぜ 20 Hz なのか（2026-08-31 に 2 Hz から変更）

  GY-BME280 の温度センサーは基板の熱容量に支配され、時定数は τ ≈ 150 s ある。
  一方シリンジ内の気体が壁に熱を渡す時定数は τ_gas ≈ 2 s しかない。
  この 2 つを分離して測るのがこのファームの目的である。

  鍵は「圧力チャネルには遅れが無い」こと。ピストンを固定して定容にすれば
      T_gas(t) = T_room * P(t) / P_eq          （P_eq = その区間で室温に戻ったときの圧力）
  が厳密に成り立つ。つまり圧力が「真の気体温度」、温度チャネルが「センサーの応答」
  であり、両者を同時に高レートで記録すれば、温度センサーの伝達関数
  （BME280 チップ由来の速い極と、FR4 基板由来の遅い極）をその場で同定できる。

  20 Hz を選んだ理由:
    - osrs_t x2 / osrs_p x16 の最大測定時間は
        1.25 + 2.3*2 + (2.3*16 + 0.575) = 43.2 ms  (データシート §9.1 の max 式)
      なので、この設定を保ったまま安全に回せる最短周期が 50 ms = 20 Hz。
    - osrs_p x16 を落とさないのが重要。圧力ノイズ 1.3 Pa は温度に換算すると
      1000 hPa / 300 K で 0.004 K に相当し、この実験では圧力が「基準温度計」に
      なるので、基準側のノイズは小さいほどよい。
    - τ_gas = 2.1 s に対し 42 点。ストローク (0.1 s 前後) は 2 点しか乗らないが、
      同定ではストロークをステップとして扱うので足りる。

  ■ 自己発熱について（2 Hz からの変更で必ず読むこと）

  20 Hz ではデューティ比が 75〜86% になり、自己発熱は 2 Hz のときの
  約 0.2 K から約 2.8 K に増える。これは「一定オフセット」である限り
  BME280 を較正器として使う運用（気体との差分を取る）で消える。
  危険なのは変化するときで、レートを測定の前後で切り替えると
  τ = 150 s の立ち上がりランプが生じ、unfold すると 2 K 級の偽信号になる。
  → **レートは常に固定**。測定前に 10 分間通電して自己発熱を飽和させること。
     バースト運転（測る直前だけ高レート）は、この理由でやってはいけない。

  ■ τ 同定の測定手順（このファームはこれを撮るためにある）

    0. OLED_UPDATE_MS を 0 にする（等間隔 20 Hz にするため）。書き込む。
    1. 通電したまま 10 分待つ。自己発熱を飽和させる。ここを省くと τ = 150 s の
       立ち上がりランプが乗り、unfold したとき 2 K 級の偽信号になる。
    2. ブラウザで接続 → 「τ 同定モード」にチェック → Record。
    3. 60 秒待つ（最初の平衡区間。ここが室温の基準になる）。
    4. 圧縮 → ストッパーまで一気に → そのまま 60 秒保持。
       ★保持中は絶対に動かさない。定容でないと T_gas = T_room·P/P_eq が崩れる。
    5. 膨張 → 反対の停止位置まで一気に → 60 秒保持。
    6. 4〜5 を 4 往復（合計 8 回のストローク、約 9 分）繰り返して Stop。
    7. CSV Export → 「生データ」にチェックして保存。
       列は 時間, 絶対温度(K), 摂氏, 気圧, seq, T_gas(K), 区間。
       T_gas が入力、絶対温度が出力、区間番号が独立にフィットできる単位。

    ★圧縮と膨張を交互にするのが要点。熱パルスの符号が交互に反転するので、
      区間ごとの応答を符号を合わせて足し合わせると、自己発熱のドリフトと
      室温の変動（どちらも符号が反転しない）が打ち消える。同じ向きに
      繰り返しても、この打ち消しは起きない。

    ★フィットは 2 極
        H(s) = 1 / ((1 + s*tau1)(1 + s*tau2))
      tau2（遅い＝FR4 基板）は各区間の尾の指数減衰から、
      tau1（速い＝BME280 チップ）は立ち上がりの丸まりから決まる。
      往復 8 回ぶんを重ねれば、どちらも 1% 級で決まるはず。

  ■ 時刻はデバイス側で付ける

  t_s は「変換区間の中点」の時刻。ブラウザ受信時刻ではなく BME280 が実際に
  積分していた時刻なので、BLE の遅延・まとめ送り・OLED 描画による取りこぼしが
  あっても、時定数のフィットは歪まない。等間隔を仮定せず時刻列で解析すること。

  ---------------------------------------------------------------------------
  BME280 データシート BST-BME280-DS001-10 (Rev 1.1, May 2015) より
    - §6.2   : I2C アドレスは SDO=GND で 0x76、SDO=VDDIO で 0x77
    - §9.1   : 測定時間 typ = 1 + 2*osrs_t + (2*osrs_p + 0.5)     = 37.5 ms
               測定時間 max = 1.25 + 2.3*osrs_t + (2.3*osrs_p+0.575) = 43.2 ms
    - §9.4   : IIR フィルタの応答時間 = 必要サンプル数 / データレート
*/

#include <Wire.h>
#include <Adafruit_BME280.h>
#include <Adafruit_SSD1306.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// ── 表示 ────────────────────────────────────────────────
#define SCREEN_WIDTH   128
#define SCREEN_HEIGHT   64
#define OLED_RESET      -1
#define SCREEN_ADDRESS 0x3C

// ── センサ（データシート §6.2）────────────────────────────
#define BME280_ADDRESS_PRIMARY   0x76
#define BME280_ADDRESS_SECONDARY 0x77

// ── BLE ────────────────────────────────────────────────
/* チップ固有の MAC 下位 3 バイトを付けた名前を setup() で組み立てる。
   例: PEL-BME280-A1B2C3
   同じ実験室で複数台を動かすとき、どの装置に繋いだのか取り違えないため。
   この名前は起動時に OLED にも表示され、Web アプリの画面・CSV/PNG の
   ファイル名・書き出し画像の見出しにも出る。 */
#define DEVICE_NAME_PREFIX "PEL-BME280"
/* ★16 ビット UUID として渡すこと。128 ビットの文字列で渡すと
   アドバタイズパケット (31 バイト) の 18 バイトを UUID が占有し、
   Flags(3) + TX Power(3) を引くと名前に 7 バイトしか残らず、
   デバイス名が途中で切り詰められる。16 ビットなら 4 バイトで済み、
   PEL-BME280-A1B2C3 (17 文字) が余裕で収まる。 */
#define SERVICE_UUID_16        ((uint16_t)0x181A)   // Environmental Sensing
#define CHARACTERISTIC_UUID_16 ((uint16_t)0x2A58)   // Analog
#define PACKET_BYTES     20

// ── 更新周期 ────────────────────────────────────────────
/* 20 Hz。冒頭の「なぜ 20 Hz なのか」を参照。
   ブラウザ側で任意の間隔に平均化し直すので、ここは常に最速の共通レートとして
   固定しておく（ファームは常に全速で送り、間引きは表示側の責任）。
   ★実験の途中でここを変えてはいけない。自己発熱が動いて温度が汚れる。 */
#define SAMPLE_PERIOD_MS 50

/* OLED は 1 秒に 1 回だけ描く。
   SSD1306 の全画面転送は 1024 バイトあり、I2C 100 kHz では約 90 ms かかる。
   50 ms のスロットには収まらないので、描いた回に限り 1〜2 サンプル落ちる。
   時刻をデバイス側で付けているので、等間隔を仮定しなければ解析上は無害。
     0        にすると OLED 更新を止め、完全に等間隔の 20 Hz になる
              （τ 同定の本番測定ではこれを推奨。BLE が繋がっていれば表示は不要）
     1000     通常運用。1 秒ごとに表示更新
   I2C を 400 kHz にすれば転送は約 23 ms に縮むが、BME280 まで 20〜30 cm の
   ケーブルを引いている構成では波形が鈍る。上げるなら実機で確認してから。 */
#define OLED_UPDATE_MS 1000
#define I2C_HZ         100000L

Adafruit_BME280 bme;
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

BLECharacteristic *pCharacteristic = nullptr;
bool bleConnected     = false;
bool bleWasConnected  = false;
uint32_t seq          = 0;
char devName[24]      = {0};   // 例: PEL-BME280-A1B2C3
uint32_t devId        = 0;     // MAC 下位 3 バイト。パケットに載せる装置ID

/* ── 接続間隔を詰める要求 ──────────────────────────────
   既定のままだと中央側 (macOS) が 30〜50 ms を選ぶことがあり、20 Hz の notify が
   接続イベントに乗り切らずに取りこぼす。0x0C = 15 ms, 0x18 = 30 ms（1.25 ms 単位）、
   timeout 400 = 4 s（10 ms 単位）。あくまで要求で、中央が拒否することもある。
   実際に何 Hz 届いたかはブラウザの「受信レート」表示で確認すること。

   ★ここが厄介なのは、ESP32 Arduino core の BLE ライブラリが
     (a) 下回りが Bluedroid か NimBLE かで onConnect の第 2 引数の型が違う
         （ESP32-C3 / core 3.3 系は NimBLE。Bluedroid 前提で書くと
          'esp_ble_gatts_cb_param_t' has not been declared でコンパイルが通らない）
     (b) core 3.3 で updateConnParams() が requestConnParams() に改名され、
         旧名は deprecated 警告になる
   の 2 点で分岐すること。どちらもマクロで切り分ける。
   この要求はあくまで最適化なので、環境が合わなければ黙って何もしないのが正しい。 */
#if defined(ESP_ARDUINO_VERSION) && defined(ESP_ARDUINO_VERSION_VAL) \
    && ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 3, 0)
  #define REQ_CONN_PARAMS(srv, who) (srv)->requestConnParams((who), 0x0C, 0x18, 0, 400)
#else
  #define REQ_CONN_PARAMS(srv, who) (srv)->updateConnParams((who), 0x0C, 0x18, 0, 400)
#endif

class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer *pServer) override    { bleConnected = true; }
  void onDisconnect(BLEServer *pServer) override { bleConnected = false; }

#if defined(CONFIG_NIMBLE_ENABLED)
  // NimBLE（ESP32-C3 / core 3.3 系はこちら）
  void onConnect(BLEServer *pServer, ble_gap_conn_desc *desc) {
    bleConnected = true;
    REQ_CONN_PARAMS(pServer, desc->conn_handle);
  }
#elif defined(CONFIG_BLUEDROID_ENABLED)
  // Bluedroid（無印 ESP32 / 古い core はこちら）
  void onConnect(BLEServer *pServer, esp_ble_gatts_cb_param_t *param) {
    bleConnected = true;
    REQ_CONN_PARAMS(pServer, param->connect.remote_bda);
  }
#endif
};

void setup() {
  Serial.begin(115200);
  /* ★ESP32-C3 の USB CDC はホスト（シリアルモニタ）が読み出さないと送信バッファが
     詰まり、Serial.print() がそこで無限にブロックする。シリアルモニタを開かずに
     動かすとループが数周で停止する（ハートビートが 3 回ほどで止まる症状）。
     送信タイムアウトを 0 にして「詰まったら捨てる」動作にすることで、
     電池だけの単独運転でも止まらなくなる。
     setTxTimeoutMs() は USB CDC (HWCDC) のみが持つので、
     ツール → USB CDC On Boot が Disabled のときのために #if で囲む。 */
#if ARDUINO_USB_CDC_ON_BOOT
  Serial.setTxTimeoutMs(0);
#endif
  /* XIAO ESP32C3 の Serial は native USB CDC。PC に繋がず電池だけで動かすと
     !Serial が永久に真のままになるので、2 秒で打ち切る。 */
  unsigned long t0 = millis();
  while (!Serial && millis() - t0 < 2000) delay(10);

  // XIAO ESP32C3 の I2C 既定ピンは D4 (GPIO6) = SDA, D5 (GPIO7) = SCL
  Wire.begin();
  Wire.setClock(I2C_HZ);

  if (!bme.begin(BME280_ADDRESS_PRIMARY) && !bme.begin(BME280_ADDRESS_SECONDARY)) {
    Serial.println(F("Could not find a valid BME280 sensor, check wiring or "
                     "try a different address!"));
    while (1) delay(10);
  }

  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println(F("SSD1306 disconnection"));
    for (;;) delay(10);
  }
  display.clearDisplay();

  /* オーバーサンプリングは Table 9 (indoor navigation) に準じる。
     湿度は使わないので SAMPLING_NONE。
     IIR フィルタは OFF。forced mode では 1 回の測定が 1 サンプルなので、
     FILTER_X16 (22 サンプル必要) にすると 20 Hz でも応答が 1.1 秒遅れ、
     τ_gas = 2.1 s の過渡を歪める。平均化が要るならブラウザ側でやる
     （そちらなら後から間隔を変えられるし、遅れも入らない）（§9.4, Table 6）。
     ★osrs_p を x16 から下げないこと。この実験では圧力が基準温度計であり、
       1.3 Pa (= 0.004 K 相当) というノイズの小ささがそのまま同定精度になる。
     フィルタ OFF でも圧力ノイズは 1.3 Pa = 0.013 hPa（Table 12）。
     平均化はブラウザ側で行うので、ここでは生の値を送る。 */
  bme.setSampling(Adafruit_BME280::MODE_FORCED,
                  Adafruit_BME280::SAMPLING_X2,      /* Temp. oversampling */
                  Adafruit_BME280::SAMPLING_X16,     /* Pressure oversampling */
                  Adafruit_BME280::SAMPLING_NONE,    /* Humidity (未使用) */
                  Adafruit_BME280::FILTER_OFF,
                  Adafruit_BME280::STANDBY_MS_125);

  // ── 固有のデバイス名を作る ──
  /* ESP.getEfuseMac() はチップに焼かれた MAC。その下位 3 バイトを 16 進で付ける。
     どの 3 バイトでも個体識別には十分で、実験室の数台なら重複しない。 */
  uint64_t chipMac = ESP.getEfuseMac();
  devId = (uint32_t)(chipMac & 0xFFFFFF);
  snprintf(devName, sizeof(devName), "%s-%06X", DEVICE_NAME_PREFIX, devId);
  Serial.print(F("device name: ")); Serial.println(devName);

  /* 起動時に自分の名前を OLED に出す。生徒がブラウザのデバイス選択画面で
     どれを選べばよいか、装置本体を見れば分かるようにするため。
     装置の筐体にも同じ 6 桁を書いたラベルを貼っておくとよい。 */
  display.clearDisplay();
  display.setTextColor(WHITE);
  display.setTextSize(1);
  display.setCursor(0, 4);
  display.println(F("BLE device name:"));
  display.setCursor(0, 24);
  display.println(devName);
  display.display();
  delay(2500);

  // ── BLE 初期化 ──
  BLEDevice::init(devName);
  BLEServer *pServer = BLEDevice::createServer();
  pServer->setCallbacks(new ServerCallbacks());

  BLEService *pService = pServer->createService(BLEUUID(SERVICE_UUID_16));
  pCharacteristic = pService->createCharacteristic(
      BLEUUID(CHARACTERISTIC_UUID_16),
      BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
  pCharacteristic->addDescriptor(new BLE2902());   // CCCD: notify の購読に必要
  pService->start();

  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(BLEUUID(SERVICE_UUID_16));
  pAdvertising->setScanResponse(true);
  BLEDevice::startAdvertising();

  Serial.print(F("BLE advertising as ")); Serial.println(devName);
}

void loop() {
  /* 絶対スケジュール。lastSample = now 方式だと OLED 描画などで遅れた分が
     毎周期そのまま加算されて、実効レートがじりじり落ちる。次回時刻を
     周期の整数倍で持てば、1 回遅れても次で追いつく。 */
  static uint32_t nextSample = 0;
  static uint32_t oledLast   = 0;
  static uint32_t statLast   = 0;
  static uint32_t maxCycleUs = 0;
  static uint32_t nSampled   = 0;

  uint32_t now = millis();
  if (nextSample == 0) nextSample = now;
  if ((int32_t)(now - nextSample) < 0) { delay(1); return; }   // ★delay(5) だと 50 ms 周期に対して粗すぎる
  nextSample += SAMPLE_PERIOD_MS;
  /* 何かの理由で 1 周期以上遅れたら、溜まった分は捨てて現在に追いつく
     （取り返そうとして連射すると自己発熱が跳ねるため）*/
  if ((int32_t)(millis() - nextSample) > (int32_t)SAMPLE_PERIOD_MS) nextSample = millis() + SAMPLE_PERIOD_MS;

  uint32_t cyc0 = micros();

  // ── 測定 ──────────────────────────────────────────
  /* forced mode: 1 回測定して sleep に戻る。完了までライブラリが status を待つ。
     時刻は「変換区間の中点」を採る。BME280 は 37.5 ms かけて積分しているので、
     開始時刻でも終了時刻でもなく中点がその 1 点を代表する。
     τ_gas = 2.1 s に対して 19 ms のずれは 1% にあたり、無視できる量ではない。 */
  uint32_t us0 = micros();
  if (!bme.takeForcedMeasurement()) return;
  uint32_t us1 = micros();

  float tempC    = bme.readTemperature();           // [degC]
  float tempK    = tempC + 273.15;                  // 絶対温度 [K]
  float presshPa = bme.readPressure() / 100.0F;     // Pa -> hPa
  float t_s      = (us0 + (us1 - us0) / 2) / 1000000.0F;
  nSampled++;

  // ── BLE notify（測定の直後に出す。OLED 描画を待たせない）──
  if (bleConnected && pCharacteristic) {
    uint8_t packet[PACKET_BYTES];
    memcpy(packet +  0, &t_s,      4);
    memcpy(packet +  4, &tempC,    4);
    memcpy(packet +  8, &presshPa, 4);
    memcpy(packet + 12, &seq,      4);
    memcpy(packet + 16, &devId,    4);
    pCharacteristic->setValue(packet, PACKET_BYTES);
    pCharacteristic->notify();
    seq++;
  }

  // ── OLED 表示（1 Hz。レイアウトは BME280_display_XIAO.ino と同一）──
  /* 全画面転送に 90 ms 前後かかるので、描いた回だけサンプルが 1〜2 点飛ぶ。
     t_s をデバイス側で付けてあるので、等間隔を仮定しなければ解析には響かない。
     OLED_UPDATE_MS を 0 にすると更新を止め、完全な等間隔 20 Hz になる。 */
  if (OLED_UPDATE_MS > 0 && (uint32_t)(now - oledLast) >= (uint32_t)OLED_UPDATE_MS) {
    oledLast = now;
    display.clearDisplay();
    display.setTextColor(WHITE);

    display.setTextSize(2);
    display.setCursor(0, 0);
    display.print(tempC);
    display.setTextSize(1);
    display.println(" C");

    display.setTextSize(2);
    display.println(" ");
    display.print(tempK);
    display.setTextSize(1);
    display.println(" K");

    display.setTextSize(2);
    display.println(" ");
    display.print(presshPa);
    display.setTextSize(1);
    display.println(" hPa");

    // BLE 接続中は右上に "B" を出す（生徒が接続状態を確認できるように）
    if (bleConnected) {
      display.setTextSize(1);
      display.setCursor(120, 0);
      display.print("B");
    }

    /* ハートビート: 更新のたびに右下の丸を 塗りつぶし <-> 輪郭 に切り替える。
       ・丸が交互に変わる  -> ループは回っている
       ・丸が止まっている  -> takeForcedMeasurement() が I2C の status 待ちで
                              固まっている可能性が高い（配線・プルアップ・ケーブル長）*/
    static bool beat = false;
    beat = !beat;
    if (beat) display.fillCircle(124, 59, 3, SSD1306_WHITE);
    else      display.drawCircle(124, 59, 3, SSD1306_WHITE);

    display.display();
  }

  // 切断されたら広告を再開する（生徒がリロードしても繋ぎ直せるように）
  if (bleWasConnected && !bleConnected) {
    delay(200);
    BLEDevice::startAdvertising();
    Serial.println(F("BLE disconnected -> advertising restarted"));
    nextSample = millis();
  }
  bleWasConnected = bleConnected;

  // ── 診断（1 Hz）──────────────────────────────────
  /* 20 Hz では毎サンプル print すると 115200 bps を食い潰し、
     USB CDC のバッファ詰まりでループが止まる（冒頭の注意を参照）。
     1 秒に 1 行だけ出し、そこに「実測サンプル数」と「最悪サイクル時間」を載せる。
     max cycle が SAMPLE_PERIOD_MS に迫っていたら余裕が無い。 */
  uint32_t cyc = micros() - cyc0;
  if (cyc > maxCycleUs) maxCycleUs = cyc;
  if ((uint32_t)(now - statLast) >= 1000) {
    statLast = now;
    Serial.print(tempC);     Serial.print(" C, ");
    Serial.print(presshPa);  Serial.print(" hPa, seq=");
    Serial.print(seq);
    Serial.print(", rate=");   Serial.print(nSampled);
    Serial.print(" Hz, maxCycle="); Serial.print(maxCycleUs / 1000.0F, 1);
    Serial.println(" ms");
    nSampled   = 0;
    maxCycleUs = 0;
  }
}
