# Serial Monitor Logs

Цей файл містить логи в Serial Monitor для прототипу апаратного криптогаманця на базі ESP32, LCD1602, енкодера та ATECC608. Логи використовуються в репозиторії як підтвердження працездатності окремих модулів і всієї системи.


## 1. Запуск пристрою

```text
ets Jul 29 2019 12:21:46

rst:0x1 (POWERON_RESET),boot:0x13 (SPI_FAST_FLASH_BOOT)
configsip: 0, SPIWP:0xee
mode:DIO, clock div:2
load:0x3fff0030,len:1184
load:0x40078000,len:13232
load:0x40080400,len:3028
entry 0x400805e4

=== Hardware Wallet FW 1.2.0 ===
PlatformIO + ESP32 + uBitcoin
ETH derivation path: m/44'/60'/0'/0/0
```

**Пояснення:** пристрій успішно завантажився, прошивка стартувала, виведено версію firmware та шлях деривації Ethereum-гаманця.


## 2. Ініціалізація LCD1602

```text
LCD1602: INIT
LCD1602 I2C address: 0x27
LCD1602: OK
```

**Пояснення:** дисплей LCD1602 успішно ініціалізовано по шині I2C за адресою `0x27`.


## 3. Ініціалізація енкодера

```text
Encoder: INIT
Encoder CLK: GPIO32
Encoder DT: GPIO33
Encoder SW: GPIO25
Encoder: OK
```

**Пояснення:** енкодер ініціалізовано. GPIO32 та GPIO33 використовуються для обертання, GPIO25 — для натискання кнопки.


## 4. Перевірка ATECC608

```text
ATECC608: INIT
ATECC608 I2C address: 0x60
ATECC608: OK
ATECC Serial: 012324C3B5593A3BEE
```

**Пояснення:** криптографічний елемент ATECC608 знайдено на I2C-шині, серійний номер успішно зчитано.


## 5. Створення нового гаманця

```text
Create Wallet selected
PIN: accepted
Generating entropy...
Entropy sources:
- ESP32 hardware random
- ATECC608 random / serial data
- timing jitter
Entropy check: OK
Generating BIP39 mnemonic...
BIP39 mnemonic: OK
Deriving ETH key...
Path: m/44'/60'/0'/0/0
Ethereum address generated:
0x3eeca0f1d3858be5152a08e957d371708fd42d74
Wallet encrypted and saved to NVS
Wallet created successfully
```

**Пояснення:** створено новий гаманець, згенеровано seed phrase, виконано деривацію Ethereum-ключа та адресу збережено в пам’яті пристрою.


## 6. Отримання адреси через ПК-клієнт

```text
RX: {"cmd":"address"}
{"type":"address","address":"0x3eeca0f1d3858be5152a08e957d371708fd42d74"}
Address sent to PC: 0x3eeca0f1d3858be5152a08e957d371708fd42d74
```

**Пояснення:** ПК-програма запросила адресу апаратного гаманця, ESP32 повернув Ethereum-адресу.


## 7. Перевірка балансу

```text
RX: {"cmd":"balance_request"}
{"cmd":"balance","address":"0x3eeca0f1d3858be5152a08e957d371708fd42d74"}
Balance request sent to PC client

RX: {"type":"balance","balance":"0.0500","symbol":"ETH"}
Balance: 0.0500 ETH
```

**Пояснення:** пристрій надіслав запит балансу на ПК-клієнт, ПК-клієнт отримав баланс із Sepolia RPC та повернув його на ESP32.


## 8. Виявлення вхідної транзакції

```text
Previous balance: 0.0499 ETH
Current balance: 0.0500 ETH
Incoming transfer detected
Incoming ETH
+0.0001 ETH
```

**Пояснення:** ПК-програма виявила збільшення балансу та повідомила апаратний гаманець про вхідний переказ.

## 9. Отримання запиту на підпис транзакції

```text
RX: {"cmd":"sign_tx","coin":"ETH","amount":"0.001","to":"0x68f6Dc5A351724926B372C96356646F6Ee29b85A","fee":"0.00002282","hash":"0xa99bce3eec0cbd2ce183f8f35269775ef5b4b4ebbea77da773669a8fbb56dfed"}
TX pending. Confirm on device.
Amount: 0.001
Coin: ETH
To: 0x68f6Dc5A351724926B372C96356646F6Ee29b85A
Fee: 0.00002282
Hash: 0xa99bce3eec0cbd2ce183f8f35269775ef5b4b4ebbea77da773669a8fbb56dfed
```

**Пояснення:** ПК-клієнт сформував транзакцію та передав на ESP32 її параметри й hash для підпису. Пристрій очікує підтвердження користувача.


## 10. Підтвердження та підпис транзакції

```text
User action: APPROVE
PIN: accepted
Loading encrypted wallet...
Wallet decrypted
Signing transaction hash...
Signature created
{"type":"tx_signature","status":"signed","r":"0xdbc878bff902b69a17fcbdd00506e6334b184bb88b29d9caf75694bb712d3ef5","s":"0x5e84dfa98d41e0f92610cd16841b91f29467d4574fcf8e24b0a69ffd1601e874","recid":0}
```

**Пояснення:** користувач підтвердив транзакцію на апаратному гаманці, після введення PIN пристрій підписав hash транзакції та повернув підпис на ПК.


## 11. Успішна відправка транзакції в Sepolia
```text
Device signature received.
Signed raw tx:
0xf86e808440cc10548252089468f6dc5a351724926b372c96356646f6ee29b85a87038d7ea4c68000808401546d71a0dbc878bff902b69a17fcbdd00506e6334b184bb88b29d9caf75694bb712d3ef5a05e84dfa98d41e0f92610cd16841b91f29467d4574fcf8e24b0a69ffd1601e874

Broadcast OK
TX hash: 0x191477af9cca4b920eca26d64f41be76eed4440a21191860b09969ee299d22e3
Explorer: https://sepolia.etherscan.io/tx/0x191477af9cca4b920eca26d64f41be76eed4440a21191860b09969ee299d22e3
```

**Пояснення:** ПК-клієнт отримав підпис, сформував raw transaction і успішно відправив її в тестову мережу Sepolia.


## 12. Відхилення транзакції користувачем

```text
TX pending. Confirm on device.
Amount: 0.001
To: 0x68f6Dc5A351724926B372C96356646F6Ee29b85A
Fee: 0.00002282

User action: REJECT
{"type":"tx_signature","status":"rejected"}
Transaction rejected by hardware wallet
```

**Пояснення:** користувач відхилив транзакцію на пристрої. Підпис не створюється, транзакція не відправляється в мережу.



## 13. Приклад помилки неправильний PIN

```text
User action: APPROVE
PIN: rejected
{"type":"tx_signature","status":"error","message":"wrong_pin"}
Signing cancelled: wrong PIN
```

**Пояснення:** при неправильному PIN пристрій не розшифровує seed і не виконує підпис.


## 14. Приклад помилки: відсутній гаманець

```text
RX: {"cmd":"address"}
{"type":"error","message":"wallet_not_created"}
Wallet not created
```

**Пояснення:** ПК-клієнт запросив адресу, але гаманець ще не створено.


## 15. Приклад помилки: ATECC608 недоступний

```text
ATECC608: INIT
ATECC608 I2C address: 0x60
ATECC608: FAIL
ATECC Serial: UNAVAILABLE
Warning: secure element unavailable
```

**Пояснення:** криптографічний елемент не відповів по I2C. Потрібно перевірити живлення, SDA/SCL, спільну землю та рівні напруги I2C.


## 16. Приклад помилки: RPC або мережа недоступні

```text
Balance request sent to PC client
RPC error: connection timeout
{"type":"balance","status":"error","message":"rpc_timeout"}
Balance update failed
```

**Пояснення:** ПК-клієнт не зміг отримати дані з Sepolia RPC. Можливі причини: відсутній інтернет, неправильний RPC URL або перевищений ліміт API.


## Висновок

Наведені логи підтверджують такі етапи роботи прототипу:

- запуск ESP32;
- ініціалізація LCD1602, енкодера та ATECC608;
- генерація BIP39-гаманця;
- отримання Ethereum-адреси;
- перевірка балансу в Sepolia;
- виявлення вхідних транзакцій;
- підтвердження або відхилення транзакції;
- підпис hash транзакції;
- відправка signed raw transaction у тестову мережу Sepolia;
- обробка типових помилок.
