import serial
import sqlite3
import datetime
import time

# Настройки
PORT = 'COM6' # Указать свой порт через который устройство подключено к ПК
BAUD = 9600
DB_NAME = 'water_data.db'


def init_db():
    conn = sqlite3.connect(DB_NAME)
    conn.execute('''CREATE TABLE IF NOT EXISTS measurements 
                    (id INTEGER PRIMARY KEY AUTOINCREMENT,
                     uptime_seconds INTEGER,
                     real_time TEXT,
                     tds REAL, turb REAL, ph REAL, 
                     status TEXT)''')
    conn.commit()
    conn.close()


def main():
    init_db()

    print("=== СИНХРОНИЗАЦИЯ ДАННЫХ БУЯ ===")
    start_input = input("Когда был включен буёк? (ЧЧ:ММ:СС) или Enter для 'сейчас': ")

    # Определяем базовое время (сегодняшняя дата + введенное время)
    now = datetime.datetime.now()
    if start_input:
        try:
            t = datetime.datetime.strptime(start_input, "%H:%M:%S")
            start_dt = now.replace(hour=t.hour, minute=t.minute, second=t.second, microsecond=0)
        except:
            print("Неверный формат. Использую текущее время.")
            start_dt = now
    else:
        start_dt = now

    try:
        ser = serial.Serial(PORT, BAUD, timeout=2)
        time.sleep(2)
        ser.reset_input_buffer()

        print("Запрашиваю данные...")
        ser.write(b'd')

        conn = sqlite3.connect(DB_NAME)
        cursor = conn.cursor()

        is_dumping = False
        count = 0

        while True:
            line = ser.readline().decode('utf-8', errors='ignore').strip()

            if "---START_DUMP---" in line:
                is_dumping = True
                continue
            if "---END_DUMP---" in line:
                break

            if is_dumping and line:
                parts = line.split(',')
                if len(parts) == 5:
                    try:
                        upt = int(parts[0])
                        tds, turb, ph, stat = float(parts[1]), float(parts[2]), float(parts[3]), parts[4]

                        # Расчет реального времени замера
                        actual_dt = start_dt + datetime.timedelta(seconds=upt)
                        actual_str = actual_dt.strftime("%Y-%m-%d %H:%M:%S")

                        cursor.execute(
                            "INSERT INTO measurements (uptime_seconds, real_time, tds, turb, ph, status) VALUES (?,?,?,?,?,?)",
                            (upt, actual_str, tds, turb, ph, stat))
                        count += 1
                        print(f"[{actual_str}] Добавлено: TDS={tds}, pH={ph}, turb={turb}")
                    except ValueError:
                        continue  # Пропуск битых строк

        conn.commit()
        conn.close()
        ser.close()
        print(f"\nГотово! Успешно перенесено строк: {count}")

    except Exception as e:
        print(f"Ошибка связи: {e}")


if __name__ == "__main__":
    main()