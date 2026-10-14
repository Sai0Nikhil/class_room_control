# Smart Classroom & Reflection-Free Presentation Controller (v1.0 Pro)

**Target Board:** STM32 Nucleo-64 (`NUCLEO-F446RE`)  
**IDE:** STM32CubeIDE  
**Framework:** STM32 HAL / CMSIS  

---

## 1. Project Overview & 5 Advanced Additions

The **`classroomcontrol`** project is an automated IoT classroom management system designed to make projector viewing glare-free while maintaining classroom comfort, energy efficiency, and student visibility.

### 5-in-1 Integrated Feature Set:
1. **Dual-Zone Note-Taking Dimming:**
   - **Presentation Mode:** Front stage light (`PB0`) turns **100% OFF** (zero screen glare). Middle (`PB1`) & Back (`PB2`) lights dim to **20% brightness** so students can comfortably take notes.
   - **Normal Mode:** All 3 lights turn **100% ON** for full classroom illumination.
2. **LM35 Intelligent Temperature & Exhaust Fan Control:**
   - LM35 on `PA1` measures room temperature.
   - When room temperature exceeds **28.0°C**, the exhaust fan relay (`PB5`) automatically engages to circulate fresh air.
3. **PIR Motion Occupancy & Eco-Sleep Mode:**
   - PIR Sensor on `PA4` (or Nucleo User Button `PC13`).
   - If no occupancy is detected for **30 seconds**, the system shuts off all lighting and enters **ECO SLEEP** mode, logging power savings.
4. **Motorized Curtain Alert Buzzer:**
   - Piezo buzzer on `PA7` gives an audible **`beep-beep` warning** before the SG90 servo motor moves the window blinds.
5. **Live UART Telemetry, CSV Data Logger & Remote Command Console:**
   - Telemetry @ 115200 Baud streams real-time status and standard CSV entries.
   - Supports remote serial commands: `AUTO`, `PROJ`, `NORMAL`, `FAN ON`, `FAN OFF`, `CALIB`, `STATUS`.

---

## 2. Complete Hardware Connection & Pinout Table

| Module / Sensor / Actuator | STM32 Pin | Arduino Header Pin | Breadboard / Circuit Connection |
| :--- | :---: | :---: | :--- |
| **LDR Light Sensor** | `PA0` | `A0` (CN8-1) | Voltage divider junction between LDR and 10 kΩ resistor to GND |
| **LM35 Temperature Sensor** | `PA1` | `A1` (CN8-2) | VCC $\to$ 3.3V/5V, GND $\to$ GND, **VOUT $\to$ PA1** |
| **PIR Motion / Occupancy** | `PA4` | `A2` (CN8-3) | VCC $\to$ 5V, GND $\to$ GND, **OUT $\to$ PA4** |
| **Window Curtain Servo** | `PA6` | `D12` (CN9-3) | Brown/Black $\to$ GND, Red $\to$ 5V, **Orange/Yellow $\to$ PA6 (TIM3 CH1)** |
| **Curtain Warning Buzzer** | `PA7` | `D11` (CN9-4) | Positive (+) $\to$ **PA7**, Negative (-) $\to$ GND |
| **Front Row Light (Stage)** | `PB0` | `A3` (CN8-4) | **PB0** $\to$ 220 Ω Resistor $\to$ LED 1 Anode (+), Cathode $\to$ GND |
| **Middle Row Light** | `PB1` | Morpho CN10-24 | **PB1** $\to$ 220 Ω Resistor $\to$ LED 2 Anode (+), Cathode $\to$ GND |
| **Back Row Light** | `PB2` | Morpho CN10-22 | **PB2** $\to$ 220 Ω Resistor $\to$ LED 3 Anode (+), Cathode $\to$ GND |
| **Exhaust Fan Relay** | `PB5` | `D4` (CN9-5) | **PB5** $\to$ Relay IN / Fan Transistor Base |
| **Manual Override Switch** | `PB4` | `D5` (CN9-6) | **PB4** $\to$ Switch Leg 1, Switch Leg 2 $\to$ GND |
| **Recalibration / Motion Button**| `PC13` | Onboard Button `B1` | Blue User Button on Nucleo board |
| **16x2 I2C LCD (SCL)** | `PB8` | `D15` (CN5-10) | LCD SCL Pin |
| **16x2 I2C LCD (SDA)** | `PB9` | `D14` (CN5-9) | LCD SDA Pin |
| **Serial Telemetry & CSV** | `PA2`/`PA3`| ST-LINK USB | Virtual COM Port @ 115200 Baud 8N1 |

---

## 3. Step-by-Step Breadboard Wiring

### 1. Power Rails
* Connect Nucleo **`3.3V`** (CN6 Pin 4) to Breadboard **(+) Red Rail**.
* Connect Nucleo **`5V`** (CN6 Pin 5) to Breadboard **(+) Orange Rail / Extra Rail** (for Servo & LCD & Fan).
* Connect Nucleo **`GND`** (CN6 Pin 6/7) to Breadboard **(-) Blue Rail**.

### 2. LDR Voltage Divider (`PA0`)
1. Place LDR between **(+) 3.3V Rail** and Breadboard **Row 10**.
2. Place **10 kΩ Resistor** between **Row 10** and **(-) GND Rail**.
3. Connect jumper wire from **Row 10** to Nucleo **`PA0`** (Arduino `A0`).

### 3. LM35 Temperature Sensor (`PA1`)
*(Looking at the flat face of the LM35 TO-92 package with pins pointing down)*
* **Pin 1 (Left):** Connect to **(+) 3.3V Rail**
* **Pin 2 (Middle):** Connect to Nucleo **`PA1`** (Arduino `A1`)
* **Pin 3 (Right):** Connect to **(-) GND Rail**

### 4. 3x LEDs (Classroom Lights)
* **LED 1 (Front):** Nucleo **`PB0`** $\to$ 220 Ω $\to$ LED1 Anode (+), Cathode (-) $\to$ GND.
* **LED 2 (Middle):** Nucleo **`PB1`** $\to$ 220 Ω $\to$ LED2 Anode (+), Cathode (-) $\to$ GND.
* **LED 3 (Back):** Nucleo **`PB2`** $\to$ 220 Ω $\to$ LED3 Anode (+), Cathode (-) $\to$ GND.

### 5. SG90 Servo Motor (`PA6`)
* **Brown Pin:** Connect to **(-) GND Rail**
* **Red Pin:** Connect to **(+) 5V Rail**
* **Orange/Yellow Pin:** Connect to Nucleo **`PA6`** (Arduino `D12`)

### 6. Piezo Buzzer (`PA7`)
* **Long Pin (+):** Connect to Nucleo **`PA7`** (Arduino `D11`)
* **Short Pin (-):** Connect to **(-) GND Rail**

### 7. Fan Relay / Motor Indicator (`PB5`)
* **Signal Pin:** Connect to Nucleo **`PB5`** (Arduino `D4`)
* **VCC:** Connect to **5V**, **GND:** Connect to **GND**

### 8. 16x2 I2C LCD (`PB8`, `PB9`)
* **VCC:** 5V, **GND:** GND
* **SCL:** Nucleo **`PB8`** (Arduino `D15`)
* **SDA:** Nucleo **`PB9`** (Arduino `D14`)

---

## 4. Live Terminal Output & CSV Logging

Connect via PuTTY or Tera Term at **115200 Baud** to see:

```text
=======================================================
 Smart Classroom & Reflection-Free Controller (v1.0 Pro)
 Target: STM32F446RE Nucleo-64 | Baud: 115200
 Features: Dual-Zone Dimming | LM35 Fan | PIR Sleep | Buzzer
=======================================================

>>> [CALIBRATION] Starting 1.0s Ambient Light Auto-Calibration...
  -> [OK] Ambient Baseline = 1180 | Dynamic Threshold = 1580 (+400)

LDR: 1185 | Temp: 26.2C | Mode: NORMAL  | Occ: YES | Fan: OFF | Curtain:  0 deg | PwrSaved:  0W
[CSV] 1,NORMAL,1185,26.2,1,0,0,0

-- (Flashlight Shined on LDR) --
[BUZZER] *Beep-Beep* -> Curtain Closing to 90 deg...
LDR: 2740 | Temp: 26.3C | Mode: PROJ    | Occ: YES | Fan: OFF | Curtain: 90 deg | PwrSaved: 32W
[CSV] 4,PROJ,2740,26.3,1,0,90,32
```
