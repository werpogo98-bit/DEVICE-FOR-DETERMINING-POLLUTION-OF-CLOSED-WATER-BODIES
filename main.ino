#include "LittleFS.h"

// --- НАСТРОЙКИ ПИНОВ ---
const int PIN_PH = 34;   
const int PIN_TURB = 35;
const int PIN_TDS = 32;
const int PIN_TDS_POWER = 27; 

// --- НАСТРОЙКИ ГЛУБОКОГО СНА ---
#define uS_TO_S_FACTOR 1000000ULL  /* Коэффициент перевода мкс в секунды */
#define TIME_TO_SLEEP  2700        /* Время сна: 45 минут (45 * 60 секунд) */

// Переменная в RTC памяти 
// Нужна, чтобы знать общее время работы без модуля часов
RTC_DATA_ATTR int bootCount = 0; 

// --- ФУНКЦИИ ---
float averageVoltage(int pin);
float readPH(float voltage);
float readTurbidity(float voltage);
float readTDS(float voltage);
String getPollutionType(float ph, float turb, float tds);
void dumpData(); 
void clearData();

void setup() {
  Serial.begin(9600);
  
  // Увеличиваем счетчик пробуждений
  bootCount++; 
  
  // Настройка пинов управления
  pinMode(PIN_TDS_POWER, OUTPUT);
  digitalWrite(PIN_TDS_POWER, LOW); // TDS изначально выключен для экономии
  
  // Настройка АЦП для корректного чтения 3.3В
  analogSetAttenuation(ADC_11db); 
  
  // Инициализация памяти
  if (!LittleFS.begin(true)) {
    Serial.println("LittleFS Error!");
    return;
  }
  
  Serial.println("--- ЗАПУСК АВТОНОМНОГО РЕЖИМА (ПРОБУЖДЕНИЕ №" + String(bootCount) + ") ---");

  // Настраиваем таймер пробуждения на 45 минут
  esp_sleep_enable_timer_wakeup(TIME_TO_SLEEP * uS_TO_S_FACTOR);
}

void loop() {
  // В АВТОНОМНОЙ ВЕРСИИ LOOP ВЫПОЛНЯЕТСЯ 1 РАЗ, ЗАТЕМ СОН
  
  // --- ЛОГИКА ИЗМЕРЕНИЙ (СХЕМА: РАЗДЕЛЬНОЕ ПИТАНИЕ) ---

  // ЭТАП 1: Измеряем pH и Мутность (TDS выключен, помех нет)
  Serial.println("Step 1: Measuring pH & Turbidity...");
  digitalWrite(PIN_TDS_POWER, LOW); 
  
  // Пауза для стабилизации питания после пробуждения
  delay(500); 
  
  float vPH = averageVoltage(PIN_PH);
  float vTurb = averageVoltage(PIN_TURB);
  
  float valPH = readPH(vPH);
  float valTurb = readTurbidity(vTurb);
  
  // ЭТАП 2: Измеряем TDS
  Serial.println("Step 2: Measuring TDS...");
  digitalWrite(PIN_TDS_POWER, HIGH); // Включаем датчик
  delay(2000); // Даем 2 секунды на прогрев датчика после сна
  
  float vTDS = averageVoltage(PIN_TDS);
  float valTDS = readTDS(vTDS);
  
  digitalWrite(PIN_TDS_POWER, LOW); // Сразу выключаем для экономии энергии
  
  // ЭТАП 3: Анализ и Сохранение
  String status = getPollutionType(valPH, valTurb, valTDS);
  
  // Рассчитываем виртуальное время работы (кол-во пробуждений * 45 мин)
  // Это заменяет millis(), который сбрасывается при сне
  unsigned long totalSeconds = bootCount * TIME_TO_SLEEP; 
  
  String dataRow = String(totalSeconds) + "," + String(valTDS, 1) + "," + String(valTurb, 1) + "," + String(valPH, 2) + "," + status;
  
  // Запись в энергонезависимую память
  File file = LittleFS.open("/data.csv", FILE_APPEND);
  if (file) {
    file.println(dataRow);
    file.close();
    Serial.println("SAVED: " + dataRow);
  } else {
    Serial.println("Error writing file");
  }

  // ЭТАП 4: Переход в глубокий сон
  Serial.println("Entering Deep Sleep for 45 minutes...");
  Serial.flush(); // Ждем, пока текст уйдет в порт
  
  esp_deep_sleep_start();
  
 
}

// --- ВСПОМОГАТЕЛЬНЫЕ ФУНКЦИИ ---

float averageVoltage(int pin) {
  float sum = 0;
  // Делаем больше выборок (30) для точности, так как спешить некуда
  for (int i = 0; i < 30; i++) {
    sum += analogRead(pin) * (3.3 / 4095.0);
    delay(10);
  }
  return sum / 30.0;
}

float readPH(float voltage) {
  float val = 7.0 + ((2.5 - voltage) * 3.5);
  return (val < 0) ? 0 : (val > 14 ? 14 : val);
}

float readTurbidity(float voltage) {
  float raw = (voltage / 3.3) * 4095.0;
  float index = (650.0 - raw) * 0.3; 
  if (index < 0) index = 0;
  return index;
}

float readTDS(float voltage) {
  return voltage * 500;
}

String getPollutionType(float ph, float turb, float tds) {
  // Логика классификации загрязнений
  if (ph > 8.5 && tds > 500 && turb > 15) return "Коммун. стоки";
  if (ph < 6.0 && tds > 400) return "Хим. (кислота)";
  if (ph < 6.5 && turb > 10) return "Биогенное";
  if (turb > 10 && ph >= 6.5 && ph <= 8.5) return "Механическое";
  if (tds > 600 && turb < 10) return "Минеральное";
  return "Норма";
}

void dumpData() {
  if (LittleFS.exists("/data.csv")) {
    File file = LittleFS.open("/data.csv", FILE_READ);
    while (file.available()) {
      Serial.write(file.read());
    }
    file.close();
  }
}