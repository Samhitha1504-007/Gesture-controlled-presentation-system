# GestureGlide: BLE Gesture-Controlled Presentation System

GestureGlide is a low-cost, contactless Human Interface Device (HID) built on the **ESP32** platform. It allows users to control presentations (PowerPoint, Keynote, Prezi) using intuitive hand gestures. By leveraging **Bluetooth Low Energy (BLE)**, the system identifies as a standard wireless keyboard, making it compatible with Windows, macOS, and Linux without the need for custom drivers.

## 🚀 Key Features
* **Contactless Control:** Navigate slides with "Swipe" and "Chop" gestures.
* **Plug-and-Play:** Uses BLE HID protocol to act as a native keyboard.
* **Low Latency:** Utilizes hardware interrupts to minimize response time.
* **Adaptive Feedback:** Integrated LDR adjusts status LED intensity based on room lighting to prevent audience distraction.

## 🛠 Hardware Components
The system integrates four sensors to interpret user intent:

| Sensor | Function | Command Sent |
| :--- | :--- | :--- |
| **Ultrasonic (Left)** | Detects left-hand proximity | `KEY_LEFT_ARROW` (Previous) |
| **Ultrasonic (Right)** | Detects right-hand proximity | `KEY_RIGHT_ARROW` (Next) |
| **IR Break-beam** | Detects "Chop" or "Hold" | `KEY_MEDIA_PLAY_PAUSE` |
| **LDR (Photoresistor)** | Ambient light sensing | Dynamic LED Dimming (PWM) |

## 🏗 System Architecture
The ESP32 serves as the central hub. The architecture is designed for efficiency:
1. **Input:** Sensors trigger **External Interrupts** to wake the processing loop.
2. **Logic:** The gesture algorithm filters noise and validates movement patterns.
3. **Output:** Validated commands are encapsulated into HID reports and sent via the BLE stack to the host PC.

## 💻 Technical Implementation
The project is implemented in **C++** using the Arduino framework for ESP32.

### Prerequisites
* [ESP32 BLE Keyboard Library](https://github.com/T-vK/ESP32-BLE-Keyboard)
* [NewPing Library](https://bitbucket.org/teckel12/arduino-newping/wiki/Home) (for optimized ultrasonic timing)

### Installation & Setup
1. **Clone the Repository:**
   ```bash
   git clone [https://github.com/yourusername/GestureGlide-ESP32.git](https://github.com/yourusername/GestureGlide-ESP32.git)
