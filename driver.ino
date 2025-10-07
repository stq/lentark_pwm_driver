#include <SoftwareSerial.h>

// Настройка задержки для UART в зависимости от частоты процессора
#if F_CPU == 8000000L
  #define CLOCK_DELAY 104
#elif F_CPU == 16000000L
  #define CLOCK_DELAY 52
#else
  // Автоматический расчет для других частот
  #define CLOCK_DELAY (1000000L / (F_CPU / 1000000L) / 19200)
#endif

// Пины подключения
#define TX_PIN 0          // P0 digispark attiny85 подключен к pin 19 mega (RX)
#define POT_PIN A2        // Потенциометр подключен к P4
#define CURRENT_PIN A3    // Сенсор тока подключен к P3
#define VOLTAGE_PIN A1    // Сенсор напряжения подключен к P2

// Программный UART для связи с ШИМ-драйвером
SoftwareSerial pwmSerial(-1, 1);

/* Параметры фильтра */
#define FILTER_HIST_SIZE 10    // Размер истории фильтра
#define FILTER_BUFF_SIZE 20    // Размер буфера фильтра

// Структура для хранения состояния фильтра
struct FilterState {
    // Конфигурация
    int bufferHysteresis;      // Гистерезис для буфера
    int stableHysteresis;      // Гистерезис для стабильного состояния
    uint8_t name;              // Идентификатор фильтра для отладки

    // Оперативные данные
    int buffer[FILTER_BUFF_SIZE] = {0};  // Буфер сырых значений
    int hist[FILTER_HIST_SIZE] = {0};    // История отфильтрованных значений
    int bufferIndex = 0;                 // Текущий индекс буфера
    int histIndex = 0;                   // Текущий индекс истории
};

// Экземпляры фильтров для разных сенсоров
FilterState currentFilter;
FilterState voltageFilter;
FilterState potFilter;

// Прототипы функций
int convertToLogarithmicDutySimple(int potValue);
void setFreq();
void setDutyGoal(int newGoal);
int changePWMDuty();
int calculateMode(int* arr, int size);
int calculateMedian(FilterState& filter);
int calculateTrimmedMean(int* arr, int size, int trimCount);
void sortFilterBuffer(FilterState& filter);
void sortFilterHist(FilterState& filter);
void initFilter(FilterState& filter, uint8_t name, int initValue, int buffHyst, int stableHyst);
void addToFilterBuffer(FilterState& filter, int value);
int filterFn(FilterState& filter, int value);

/* Отладочный вывод через программный UART */
volatile bool debugTransmission = false;  // Флаг блокировки передачи при отладке

// Отправка одного байта через программный UART
void printChar(uint8_t data) {
  // Стартовый бит
  digitalWrite(TX_PIN, LOW);
  delayMicroseconds(CLOCK_DELAY);

  // Биты данных (младший бит первый)
  for(uint8_t i = 0; i < 8; i++) {
    digitalWrite(TX_PIN, data & 1);
    data >>= 1;
    delayMicroseconds(CLOCK_DELAY);
  }

  // Стоповый бит
  digitalWrite(TX_PIN, HIGH);
  delayMicroseconds(CLOCK_DELAY);
}

void beginDebug(){ debugTransmission = true; }
void endDebug(){ debugTransmission = false; }

// Вывод строки
void printString(const char* str) {
  while(*str) {
    printChar(*str++);
  }
}

// Перевод строки
void newline() {
  printChar('\r');
  printChar('\n');
}

// Вывод числа
void printNum(unsigned long n) {
  if (n >= 10) printNum(n / 10);
  printChar('0' + (n % 10));
}

// Вычисление моды (наиболее часто встречающегося значения)
int calculateMode(int* arr, int size) {
    int maxCount = 0;
    int mode = arr[0];
    int current = arr[0];
    int currentCount = 1;

    for (int i = 1; i < size; i++) {
        if (arr[i] == current) {
            currentCount++;
        } else {
            if (currentCount > maxCount) {
                maxCount = currentCount;
                mode = current;
            }
            current = arr[i];
            currentCount = 1;
        }
    }

    // Проверка последнего элемента
    if (currentCount > maxCount) {
        mode = current;
        maxCount = currentCount;
    }

    // Если все значения уникальные, считаем что моды нет
    return (maxCount > 1) ? mode : -1;
}

// Вычисление медианы из отсортированного буфера
int calculateMedian(FilterState& filter) {
    return filter.buffer[FILTER_BUFF_SIZE / 2];
}

// Вычисление усеченного среднего (игнорируем крайние значения)
int calculateTrimmedMean(int* arr, int size, int trimCount) {
    if (size <= 2 * trimCount) return arr[size / 2]; // fallback to median

    int sum = 0;
    for (int i = trimCount; i < size - trimCount; i++) {
        sum += arr[i];
    }
    return sum / (size - 2 * trimCount);
}

// Сортировка буфера пузырьком
void sortFilterBuffer(FilterState& filter) {
    for (int i = 0; i < FILTER_BUFF_SIZE - 1; i++) {
        for (int j = 0; j < FILTER_BUFF_SIZE - i - 1; j++) {
            if (filter.buffer[j] > filter.buffer[j + 1]) {
                int temp = filter.buffer[j];
                filter.buffer[j] = filter.buffer[j + 1];
                filter.buffer[j + 1] = temp;
            }
        }
    }
}

// Сортировка истории пузырьком
void sortFilterHist(FilterState& filter) {
    for (int i = 0; i < FILTER_HIST_SIZE - 1; i++) {
        for (int j = 0; j < FILTER_HIST_SIZE - i - 1; j++) {
            if (filter.hist[j] > filter.hist[j + 1]) {
                int temp = filter.hist[j];
                filter.hist[j] = filter.hist[j + 1];
                filter.hist[j + 1] = temp;
            }
        }
    }
}

// Инициализация фильтра
void initFilter(FilterState& filter, uint8_t name, int initValue, int buffHyst, int stableHyst) {
    filter.bufferHysteresis = buffHyst;
    filter.stableHysteresis = stableHyst;
    filter.name = name;

    // Заполнение буферов начальными значениями
    for(int i = 0; i < FILTER_BUFF_SIZE; i++) {
        filter.buffer[i] = initValue;
    }
    for(int i = 0; i < FILTER_HIST_SIZE; i++) {
        filter.hist[i] = initValue;
    }

    filter.bufferIndex = 0;
    filter.histIndex = 0;
}

// Добавление значения в буфер фильтра
void addToFilterBuffer(FilterState& filter, int value) {
    filter.buffer[filter.bufferIndex] = value;
    filter.bufferIndex++;

    // При заполнении буфера выполняем анализ
    if(filter.bufferIndex >= FILTER_BUFF_SIZE) {
        filter.bufferIndex = 0;

        sortFilterBuffer(filter);
        int median = calculateMedian(filter);
        int mode = calculateMode(filter.buffer, FILTER_BUFF_SIZE);

        beginDebug();

        // Вывод минимального и максимального значения из буфера
        printNum(filter.buffer[0]);
        printChar('~');
        printNum(filter.buffer[FILTER_BUFF_SIZE-1]);
        printChar(' ');

        // Выбор между медианой и модой
        int result;
        if (mode != -1 && abs(mode - median) <= filter.bufferHysteresis) {
            result = mode;  // Используем моду если она близка к медиане
            printChar(' ');
            printNum(median);
            printChar(' ');
            printChar('/');
            printChar('(');
            printNum(mode);
            printChar(')');
            printChar(' ');
        } else {
            result = median; // Иначе используем медиану
            printChar('(');
            printNum(median);
            printChar(')');
            printChar('/');
            printChar(' ');
            printNum(mode);
            printChar(' ');
            printChar(' ');
        }

        if( filter.name == 'R' ) newline();
        endDebug();

        // Добавление результата в историю
        filter.hist[filter.histIndex] = result;
        filter.histIndex = (filter.histIndex + 1) % FILTER_HIST_SIZE;
    }
}

// Основная функция фильтрации
int filterFn(FilterState& filter, int value) {
    addToFilterBuffer(filter, value);

    // Если история еще не заполнена, возвращаем последнее значение
    if (filter.histIndex < FILTER_HIST_SIZE - 1) {
        return filter.buffer[(filter.bufferIndex == 0) ? FILTER_BUFF_SIZE - 1 : filter.bufferIndex - 1];
    }

    // Сортируем историю и вычисляем усеченное среднее
    sortFilterHist(filter);
    return calculateTrimmedMean(filter.hist, FILTER_HIST_SIZE, 2);
}

void setup() {
  pinMode(3, INPUT);
  pinMode(2, INPUT);
  pinMode(1, OUTPUT);

  pwmSerial.begin(4800);

  pinMode(TX_PIN, OUTPUT);
  digitalWrite(TX_PIN, HIGH); // Установка стоп-бита

  delay(200);
  setFreq();

  // Инициализация фильтров с разными параметрами
  initFilter(currentFilter, 'C', analogRead(CURRENT_PIN), 2, 0);
  initFilter(voltageFilter, 'V', analogRead(VOLTAGE_PIN), 2, 0);
  initFilter(potFilter, 'R', analogRead(POT_PIN), 30, 0);
}

// Счетчик для регулирования частоты обновления ШИМ
int dct = FILTER_BUFF_SIZE;

void loop() {
  delay(10);

  // Чтение и фильтрация данных с сенсоров
  int filteredCurrent = filterFn(currentFilter, analogRead(CURRENT_PIN));
  int filteredVoltage = filterFn(voltageFilter, analogRead(VOLTAGE_PIN));
  int potValue = filterFn(potFilter, analogRead(POT_PIN));

  // Расчет целевой скважности с логарифмической характеристикой
  int calculatedDutyGoal = convertToLogarithmicDutySimple(potValue);

  // Округление для больших значений
  if( calculatedDutyGoal >= 1000 ) {
    calculatedDutyGoal = round((float)calculatedDutyGoal/1000.0f)*1000;
  } else {
    calculatedDutyGoal = 100;
  }

  // Ограничение максимальной скважности
  int dutyGoalLimit = 10000;
  int finalDutyGoal = min(calculatedDutyGoal, dutyGoalLimit);

  // Периодическое обновление ШИМ
  if( dct-- == 0 ) {
    dct = FILTER_BUFF_SIZE;
    setDutyGoal(finalDutyGoal);
    changePWMDuty();
  }
}

// Текущая и целевая скважность ШИМ
int goalDuty = 900;
int currentDuty = 900;
int lastSetDuty = 900;

// Установка целевой скважности с гистерезисом
void setDutyGoal(int newGoal) {
  if(abs(newGoal - goalDuty) < 50) return; // Гистерезис для избежания дребезга
  goalDuty = newGoal;
}

// Плавное изменение скважности ШИМ
int changePWMDuty(){
  if(debugTransmission) return;

  // Плавное изменение с адаптивным шагом
  if(currentDuty < goalDuty) {
    currentDuty += max(1, abs(currentDuty - goalDuty)/10);
  } else if(currentDuty > goalDuty) {
    currentDuty -= max(1, abs(currentDuty - goalDuty)/10);
  }

  // Ограничение диапазона
  currentDuty = constrain(currentDuty, 0, 10000);

  // Отправка команды только при изменении значения
  if(currentDuty != lastSetDuty) {
    digitalWrite(1, HIGH);  // Индикация передачи
    lastSetDuty = currentDuty;

    // Формирование команды для ШИМ-драйвера
    pwmSerial.print("2D");
    if(currentDuty == 10000) {
      pwmSerial.print("100.00");
    } else {
      int d1 = currentDuty / 1000;
      int d2 = (currentDuty % 1000) / 100;
      int d3 = (currentDuty % 100) / 10;
      int d4 = currentDuty % 10;
      pwmSerial.print(d1);
      pwmSerial.print(d2);
      pwmSerial.print(".");
      pwmSerial.print(d3);
      pwmSerial.print(d4);
    }
    pwmSerial.print(">");
    pwmSerial.flush();
  } else {
    digitalWrite(1, LOW);  // Выключение индикации
  }
  delay(30);
}

// Преобразование линейного значения потенциометра в логарифмическую скважность
int convertToLogarithmicDutySimple(int potValue) {
  float normalized = (float)potValue / 1023.0;
  float logarithmic = normalized * normalized;  // Квадратичная характеристика
  int result = (int)(logarithmic * 10000.0);
  return constrain(result, 0, 10000);
}

// Установка частоты ШИМ
void setFreq() {
  pwmSerial.print("2U2500.00>");
  pwmSerial.flush();
  delay(120);
}