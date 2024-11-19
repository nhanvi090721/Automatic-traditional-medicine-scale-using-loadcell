// Do An Cam Bien Chuyen Nang - Nhom 03
#include <HX711.h>
#include <Keypad.h>
#include <ESP32_Servo.h>
#include <LiquidCrystal_I2C.h>

HX711 scale;
Servo myServo;
LiquidCrystal_I2C lcd(0x27, 20, 4);

#define LOADCELL_DOUT_PIN 25
#define LOADCELL_SCK_PIN 26
// #define CALIBRATION_FACTOR 395
#define CALIBRATION_FACTOR 420
#define DIR 33
#define STEP 27
#define STEPPER_STEPS 1600
#define servoPin 12
#define NUM_OH 9
#define NUM_INGREDIENTS 9
#define NUM_RECIPES 2
#define MAX_QUANTITY 5
#define EN 14

hw_timer_t* timer = NULL;
volatile SemaphoreHandle_t timerSemaphore;
volatile SemaphoreHandle_t mocua;
volatile SemaphoreHandle_t dongcua;
volatile SemaphoreHandle_t canthuoc;
portMUX_TYPE timerMux = portMUX_INITIALIZER_UNLOCKED;

uint8_t start_angle = 74;
uint8_t end_angle = 110;
volatile uint8_t angle;
bool isProcessing = false;  // Tránh lặp lại thông báo khi đang xử lý
bool flag = 0;
float weight = 0;
int cnt = 0;
int currentSection = 0;  // Vị trí khởi động ban đầu tại ô Tao do
int recipeQuantity = 0;
int selectedRecipeIndex = -1;
int stepsPerSection = STEPPER_STEPS / NUM_OH;

const String ingredients[NUM_INGREDIENTS] = {
  "Tao Do", "Ky Tu", "Re Tranh", "Hoa Cuc", "Nhan Tran",
  "Cam Thao", "Vien Chi", "Long Nhan", "Toan Tao Nhan"
};

float ingredientQuantities[NUM_RECIPES][NUM_INGREDIENTS] = {
  { 2.5, 1, 1, 1, 2, 0, 0, 0, 0 },       // Trà Hoa Cúc
  { 0, 1, 0, 0, 0, 0.8, 0.8, 1.5, 1.8 }  // Hao Mien Thang
};

const byte ROWS = 4;
const byte COLS = 4;

char keys[ROWS][COLS] = {
  { '1', '2', '3', 'A' },
  { '4', '5', '6', 'B' },
  { '7', '8', '9', 'C' },
  { '*', '0', '#', 'D' }
};
byte rowPins[ROWS] = { 23, 19, 18, 5 };
byte colPins[COLS] = { 17, 16, 4, 15 };
Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

void ARDUINO_ISR_ATTR onTimer() {
  // Increment the counter and set the time of ISR
  portENTER_CRITICAL_ISR(&timerMux);

  portEXIT_CRITICAL_ISR(&timerMux);
  // Give a semaphore that we can check in the loop
  if (flag == 1) { cnt++; }


  // xSemaphoreGiveFromISR(timerSemaphore, NULL);
  // It is safe to use digitalRead/Write here if you want to toggle an output
}


static void mo_cua_task(void* arg) {
  for (;;) {
    if (xSemaphoreTake(mocua, portMAX_DELAY) == pdTRUE) {
      end_angle = 110;
      for (angle = start_angle; angle <= end_angle; angle++) {
        Serial.println("tui dang o day ne");
        myServo.write(angle);
        vTaskDelay(125 / portTICK_PERIOD_MS);
      }
    }
  }
}

static void dong_cua_task(void* arg) {
  const TickType_t xFrequency = pdMS_TO_TICKS(2);
  TickType_t xLastWakeTime = xTaskGetTickCount();
  for (;;) {
    if (xSemaphoreTake(dongcua, portMAX_DELAY) == pdTRUE) {

      weight = 0;
      for (int x = angle; angle >= start_angle; angle--) {
        myServo.write(angle);
        vTaskDelayUntil(&xLastWakeTime, xFrequency);  // Chờ đến lần tiếp theo
      }
    }
  }
}

static void can_thuoc_task(void* arg) {
  for (;;) {
    if (xSemaphoreTake(canthuoc, portMAX_DELAY) == pdTRUE) {
      scale.set_scale(CALIBRATION_FACTOR);  // Ensure calibration factor is applied                // Number of readings to average
      float totalWeight = 0;
      totalWeight += (scale.get_units(10) + 0.2);
      if (totalWeight <= 0.3) { totalWeight = 0; }
      vTaskDelay(2 / portTICK_PERIOD_MS);
      weight = totalWeight;
    }
  }
}

void Menu() {
  unsigned long previousMillis = 0;
  unsigned long interval = 1500;
  lcd.clear();
  lcd.setCursor(1, 1);
  lcd.print("TD FOOD Kinh Chao!");
  previousMillis = millis();  // Lưu thời gian hiện tại
  while (millis() - previousMillis < interval) {
    // Chương trình sẽ không bị gián đoạn trong khi đợi
  }
  lcd.clear();
  lcd.setCursor(2, 0);
  lcd.print("Chon bai thuoc:");
  lcd.setCursor(2, 1);
  lcd.print("1. Tra Hoa Cuc");
  lcd.setCursor(2, 2);
  lcd.print("2. Hao Mien Thang");
  lcd.setCursor(0, 3);  // Separator row
  lcd.print("====================");
}

// Hàm bấm nút '*' để quay lại hiển thị Menu
bool Check_return_to_Menu() {
  char key = keypad.getKey();
  if (key == '*' && key != '#') {  // Thêm kiểm tra phím cần thiết
    lcd.clear();
    Menu();
    return true;
  }
  return false;
}

// Hàm xử lý phím '#' để chuyển ngay đến màn hình chọn số lượng
bool Check_continue_to_quantity() {
  char key = keypad.getKey();
  if (key == '#' && key != '*') {
    SoLuong(selectedRecipeIndex);
    return true;
  }
  return false;
}

void Tra_Hoa_Cuc() {
  selectedRecipeIndex = 0;
  Hien_Thi_Thanh_Phan_Tra_Hoa_Cuc(0);
}

void Hao_Mien_Thang() {
  selectedRecipeIndex = 1;
  Hien_Thi_Thanh_Phan_Hao_Mien_Thang(1);
}

void Hien_Thi_Thanh_Phan_Tra_Hoa_Cuc(int recipeIndex) {
  unsigned long previousMillis = 0;
  unsigned long interval = 2000;
  lcd.clear();
  lcd.setCursor(0, 1);
  lcd.print("Ban chon Bai Thuoc:");
  lcd.setCursor(0, 2);
  lcd.print("Tra Hoa Cuc(7.5 gr)");
  previousMillis = millis();  // Lưu thời gian hiện tại
  while (millis() - previousMillis < interval) {
    // Chương trình sẽ không bị gián đoạn trong khi đợi
  }
  lcd.clear();
  lcd.setCursor(5, 0);
  lcd.print("Thanh phan");  // Cho xem thành phần
  //======= Thanh phan [Tao do]: 6g =======//
  lcd.setCursor(0, 1);
  lcd.print("1.Tao do    (2.5g)");
  //======= Thanh phan [Ky tu]: 2g ==========//
  lcd.setCursor(0, 2);
  lcd.print("2.Ky Tu      (1g)");
  previousMillis = millis();  // Lưu thời gian hiện tại
  while (millis() - previousMillis < interval) {
    // Chương trình sẽ không bị gián đoạn trong khi đợi
  }
  lcd.clear();  // Xoa man hinh hien thi 3 nguyen lieu tiep theo
  lcd.setCursor(5, 0);
  lcd.print("Thanh phan");  // Cho xem thành phần
  //======= Thanh phan [Re tranh]: 3g =======//
  lcd.setCursor(0, 1);
  lcd.print("3.Re Tranh  (1g)");
  //======= Thanh phan [Hoa cuc]: 2g =======//
  lcd.setCursor(0, 2);
  lcd.print("4.Hoa cuc   (1g)");
  //======= Thanh phan [Nhan tran]: 2g =======//
  lcd.setCursor(0, 3);
  lcd.print("5.Nhan Tran (2g)");
  previousMillis = millis();  // Lưu thời gian hiện tại
  while (millis() - previousMillis < interval) {
    // Chương trình sẽ không bị gián đoạn trong khi đợi
  }
  // Lưu recipeIndex khi người dùng chọn bài thuốc
  selectedRecipeIndex = 0;  // Tra Hoa Cuc là bài thuốc với chỉ số 1
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Quay ve Menu, chon *");
  lcd.setCursor(0, 2);
  lcd.print("Tiep tuc, chon #");
  // Chờ người dùng nhấn '#'
  while (true) {
    if (Check_continue_to_quantity()) { return; }
    if (Check_return_to_Menu()) { return; }
  }
}

void Hien_Thi_Thanh_Phan_Hao_Mien_Thang(int recipeIndex) {
  unsigned long previousMillis = 0;
  unsigned long interval = 2000;
  lcd.clear();
  lcd.setCursor(0, 1);
  lcd.print("Ban chon Bai Thuoc:");
  lcd.setCursor(0, 2);
  lcd.print("Hao Mien Thang");
  lcd.setCursor(12, 3);
  lcd.print("(5.9 gr)");
  previousMillis = millis();  // Lưu thời gian hiện tại
  while (millis() - previousMillis < interval) {
    // Chương trình sẽ không bị gián đoạn trong khi đợi
  }
  lcd.clear();
  lcd.setCursor(5, 0);
  lcd.print("Thanh phan");  // Cho xem thành phần
  //======= Thanh phan [Ky tu]: 1g ==========//
  lcd.setCursor(0, 1);
  lcd.print("1.Ky Tu       (1g)");
  //======= Thanh phan [Cam Thao]: 1g =======//
  lcd.setCursor(0, 2);
  lcd.print("2.Cam Thao   (0.8g)");
  previousMillis = millis();  // Lưu thời gian hiện tại
  while (millis() - previousMillis < interval) {
    // Chương trình sẽ không bị gián đoạn trong khi đợi
  }
  lcd.clear();  // Xoa man hinh hien thi 3 nguyen lieu tiep theo
  lcd.setCursor(5, 0);
  lcd.print("Thanh phan");  // Cho xem thành phần
  //======= Thanh phan [Vien Chi]: 2 g =======//
  lcd.setCursor(0, 1);
  lcd.print("3.Vien Chi    (0.8g)");
  //======= Thanh phan [Long Nhan]: 2 g =======//
  lcd.setCursor(0, 2);
  lcd.print("4.Long Nhan   (1.5g)");
  //======= Thanh phan [Toan Tao Nhan]: 5 g =======//
  lcd.setCursor(0, 3);
  lcd.print("5.T.Tao Nhan  (1.8g)");
  previousMillis = millis();  // Lưu thời gian hiện tại
  while (millis() - previousMillis < interval) {
    // Chương trình sẽ không bị gián đoạn trong khi đợi
  }
  // Lưu recipeIndex khi người dùng chọn bài thuốc
  selectedRecipeIndex = 1;  // Hao Mien Thang là bài thuốc với chỉ số 2
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Quay ve Menu, chon *");
  lcd.setCursor(0, 2);
  lcd.print("Tiep tuc, chon #");
  // Chờ người dùng nhấn '#'
  while (true) {
    if (Check_continue_to_quantity()) { return; }
    if (Check_return_to_Menu()) { return; }
  }
}

void ChonThuoc() {
  char key = keypad.getKey();
  switch (key) {
    case '1':
      Tra_Hoa_Cuc();
      break;
    case '2':
      Hao_Mien_Thang();
      break;
  }
}

void SoLuong(int selectedRecipeIndex) {
  String quantity = "";
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Hay nhap so luong !");
  delay(100);
  lcd.setCursor(0, 2);
  lcd.print("So luong da nhan: ");
  lcd.setCursor(18, 2);
  lcd.print("   ");

  while (true) {
    char key = keypad.getKey();
    if (key) {
      if (key == '*') {  // Check for * key to return to the main menu
        lcd.clear();
        Menu();  // Quay lại menu chính
        return;  // Exit SoLuong function
      }
      if (key >= '0' && key <= '9') {  // Nếu người dùng nhấn số
        quantity += key;
        lcd.setCursor(18, 2);   // Cập nhật lại vị trí số lượng
        lcd.print(quantity);    // Hiển thị số lượng đã nhập
      } else if (key == 'A') {  // Khi nhấn nút A (xác nhận)
        int qty = quantity.toInt();
        if (qty <= MAX_QUANTITY && qty > 0) {
          recipeQuantity = qty;
          Chon_Bai_Thuoc(selectedRecipeIndex);
          break;
        } else {
          // Display "Too High Quantity" message
          lcd.clear();
          lcd.setCursor(0, 0);
          lcd.print("Vuot qua So luong !");
          lcd.setCursor(1, 2);
          lcd.print("Vui long nhap lai.");
          delay(2000);  // Show the error for 2 seconds
          lcd.clear();  // Clear the error message
          lcd.setCursor(0, 0);
          lcd.print("Hay nhap so luong !");  // Prompt for re-entry
          lcd.setCursor(0, 2);
          lcd.print("So luong da nhan: ");
          quantity = "";  // Reset quantity input
          lcd.setCursor(18, 2);
          lcd.print("   ");  // Clear the entered quantity
        }
      } else if (key == 'C') {  // Khi nhấn nút C để xóa số đã nhập
        quantity = "";          // Xóa số đã nhập
        lcd.setCursor(18, 2);   // Vị trí cần làm sạch
        lcd.print("   ");       // Xóa số đã nhập
      }
    }
  }
}

void Chon_Bai_Thuoc(int recipeIndex) {
  unsigned long previousMillis = 0;
  unsigned long interval = 2000;
  for (int count = 0; count < recipeQuantity; count++) {
    for (int i = 0; i < NUM_INGREDIENTS; i++) {
      if (ingredientQuantities[recipeIndex][i] > 0) {
        Quay_Den_O_Thuoc(i, ingredients[i], count);
      }
    }
    Quay_Ve_O_Bat_Dau(0);
    // Display completion message for each cycle
    lcd.clear();
    lcd.setCursor(3, 0);  // Centered text
    lcd.print("Hoan thanh lan:");
    // lcd.setCursor(8, 1);
    lcd.print(count + 1);  // Show cycle count
    lcd.setCursor(0, 2);   // Bottom row separator
    lcd.print("====================");
    previousMillis = millis();  // Lưu thời gian hiện tại
    while (millis() - previousMillis < interval) {
      // Chương trình sẽ không bị gián đoạn trong khi đợi
    }
  }
  // Final completion message
  lcd.clear();
  lcd.setCursor(0, 0);  // Centered
  lcd.print("** Da hoan thanh! **");
  lcd.setCursor(1, 1);
  lcd.print("TD FOOD xin cam on!");
  lcd.setCursor(4, 2);  // Centered farewell
  lcd.print("Hen gap lai !");
  lcd.setCursor(0, 3);  // Bottom separator
  lcd.print("====================");
  digitalWrite(EN, HIGH);
  previousMillis = millis();  // Lưu thời gian hiện tại
  while (millis() - previousMillis < interval) {
    // Chương trình sẽ không bị gián đoạn trong khi đợi
  }
  Menu();  // Return to main menu
  isProcessing = false;
}

void Quay_Den_O_Thuoc(int targetSection, const String& ingredientName, int cycleCount) {
  // unsigned long previousMillis = 0;
  // unsigned long interval = 2000;
  int stepsToMove = ((targetSection - currentSection + NUM_OH) % NUM_OH) * stepsPerSection;
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Dang lay nguyen lieu");
  lcd.setCursor(1, 1);
  lcd.print("Ten: ");  // Displaying the ingredient name
  lcd.print(ingredientName);
  lcd.setCursor(1, 2);
  lcd.print("Lan: ");
  lcd.print(cycleCount + 1);
  lcd.print(" / ");
  lcd.print(recipeQuantity);
  lcd.setCursor(1, 3);  // Line 4
  lcd.print("KLHT: ");

  digitalWrite(EN, LOW);
  digitalWrite(DIR, LOW);

  for (int i = 0; i < stepsToMove; i++) {
    digitalWrite(STEP, HIGH);
    delay(8);
    digitalWrite(STEP, LOW);
    delay(8);
  }

  // Tắt động cơ khi hoàn thành
  digitalWrite(EN, HIGH);
  currentSection = targetSection;
  // Serial.println("bo dang o day");
  // xTaskCreate(can_thuoc_task, "can_thuoc_task", 4096, NULL, 5, NULL);
  //xTaskCreate(mo_cua_task, "mo_cua_task", 4096, NULL, 5, NULL);
  vTaskResume(xTaskGetHandle("mo_cua_task"));
  vTaskResume(xTaskGetHandle("can_thuoc_task"));
  vTaskSuspend(xTaskGetHandle("dong_cua_task"));
  MoCua();
  scale.tare();

  float currentWeight = 0.0;
  float requiredWeight = ingredientQuantities[selectedRecipeIndex][targetSection];
  float weightTolerance = 0.2;  // Sai số cho phép là 0.2 gram

  while (currentWeight < requiredWeight) {
    xSemaphoreGive(canthuoc);
    currentWeight = weight;
    lcd.setCursor(6, 3);
    lcd.print(currentWeight, 1);
    lcd.print(" / ");
    lcd.print(requiredWeight, 1);
    lcd.print(" g");
    // Xóa Task1 bằng tên
    // if ((currentWeight >= (requiredWeight - weightTolerance)) && (currentWeight <= (requiredWeight + weightTolerance))) {
    //   break;  // Dừng lại khi khối lượng đã đạt yêu cầu
    // }




    // if ((currentWeight >= (requiredWeight - weightTolerance)) || (currentWeight <= (requiredWeight + weightTolerance))) {
    //   vTaskSuspend(xTaskGetHandle("mo_cua_task"));     // Xóa Task1 bằng tên
    //   end_angle = angle;
    //    vTaskResume(xTaskGetHandle("dong_cua_task"));
    //   vTaskSuspend(xTaskGetHandle("can_thuoc_task"));  // Xóa Task1 bằng tên
    //   flag = 1;
    //   break;  // Exit loop when weight is within the allowed tolerance
    // }
  }

  vTaskSuspend(xTaskGetHandle("mo_cua_task"));  // Xóa Task1 bằng tên
  end_angle = angle;
  vTaskResume(xTaskGetHandle("dong_cua_task"));
  vTaskSuspend(xTaskGetHandle("can_thuoc_task"));  // Xóa Task1 bằng tên
  flag = 1;
  // Đóng cửa chỉ khi khối lượng đã đủ và cửa đang mở
  // if (currentWeight >= requiredWeight - weightTolerance) {
  //   DongCua();  // Đóng cửa
  // }

  DongCua();
  while (cnt < 199) {
    DongCua();
    flag = 1;
    vTaskSuspend(xTaskGetHandle("mo_cua_task"));  // Xóa Task1 bằng tên
    Serial.println("t ne ");
  }
  cnt = 0;
  flag = 0;
}

// Hàm điều khiển servo quay từ từ
void MoCua() {
  xSemaphoreGive(mocua);  // Cấp phát semaphore cho mo_cua_task
}

void DongCua() {
  xSemaphoreGive(dongcua);  // Cấp phát semaphore cho mo_cua_task
}

void Quay_Ve_O_Bat_Dau(int targetSection) {
  int stepsToMove = ((targetSection - currentSection + NUM_OH) % NUM_OH) * stepsPerSection;
  digitalWrite(EN, LOW);
  // delay(4);
  digitalWrite(DIR, LOW);
  for (int i = 0; i < stepsToMove; i++) {
    digitalWrite(STEP, HIGH);
    delay(8);
    digitalWrite(STEP, LOW);
    delay(8);
  }
  currentSection = targetSection;
}

void setup() {
  myServo.attach(servoPin);

  Serial.begin(115200);

  pinMode(EN, OUTPUT);
  digitalWrite(EN, HIGH);
  pinMode(DIR, OUTPUT);
  pinMode(STEP, OUTPUT);

  lcd.init();
  lcd.backlight();

  // Khởi tạo cảm biến load cell
  scale.begin(LOADCELL_DOUT_PIN, LOADCELL_SCK_PIN);
  scale.set_scale(CALIBRATION_FACTOR);  // Đặt hệ số hiệu chỉnh (cần hiệu chỉnh trước)
  scale.tare();

  mocua = xSemaphoreCreateBinary();
  canthuoc = xSemaphoreCreateBinary();
  dongcua = xSemaphoreCreateBinary();
  //timerSemaphore = xSemaphoreCreateBinary();
  timer = timerBegin(0, 80, true);
  // Attach onTimer function to our timer.
  timerAttachInterrupt(timer, &onTimer, true);
  timerAlarmWrite(timer, 10000, true);  // 10ms

  // Start an alarm
  timerAlarmEnable(timer);

  xTaskCreate(mo_cua_task, "mo_cua_task", 4096, NULL, 5, NULL);
  xTaskCreate(can_thuoc_task, "can_thuoc_task", 4096, NULL, 5, NULL);
  xTaskCreate(dong_cua_task, "dong_cua_task", 4096, NULL, 8, NULL);

  Menu();
}

void loop() {
  ChonThuoc();
  Check_return_to_Menu();
  // Check_continue_to_quantity();
}
