
/**
  Arduino program for giving access to restricted area used in an elevator situation.
  When you svan your card/chip you will be granted access to the fourth floor.

  author Komolvae.

  Pin layout used:
  +------------------------------------------------------------------------------------------+
  |                    MFRC522      Arduino                  Cable cat 5e                    |
  |                    Reader/LCD   Yün                      Colour                          |
  | Signal             LEDS         Pin                      Management                      |
  | /////////////////////////////////////// LCD  /////////////////////////////////////////// |
  | Ground             LCD 1        GND                      Black                           |
  | VDD(5V)            LCD 2        5V                       Red                             |
  | Cotrast Adjust     LCD 3        Midpin potetiometer                                      |
  | Register Select    LCD 4        7                        Orange white    1               |
  | Read/Write Select  LCD 5        GND                      Black                           |
  | Enable             LCD 6        6~                       Orange          1               |
  | Data Lines D4      LCD 11       5~                       Brown white     1               |
  | Data Lines D5      LCD 12       4                        Brown           1               |
  | Data Lines D6      LCD 13       3~                       Green white     1               |
  | Data Lines D7      LCD 14       2                        Green           1               |
  | Backlight Power    LCD 15       5V                       Red                             |
  | Backlight Ground   LCD 16       GND                      Black                           |
  |  /////////////////////////////////////// Power ///////////////////////////////////////// |
  | 5v                                                       Blue white      1               |
  | 0V                                                       Blue            1               |
  |                                                                                          |
  | //////////////////////////////////////// LEDS ////////////////////////////////////////// |
  | 5V Red Led         +            12                       Orange          2               |
  | 0V Red Led         -            GND + 330k ohm           Black                           |
  | 5V Green Led       +            13                       Orange white    2               |
  | 0V Green Led       -            GND + 330k ohm           Black                           |
  |                                                                                          |
  | /////////////////////////////////////// RFID /////////////////////////////////////////// |
  | RFID 3,3V                                                Brown           2               |
  | RST/Reset          RST          9                        Brown white     2               |
  | SPI SDA            SDA(SS)      10,11                    Green white     2               |
  | SPI MOSI           MOSI         ICSP-4                   Blue white      2               |
  | SPI MISO           MISO         ICSP-1                   Blue            2               |
  | SPI SCK            SCK          ICSP-3                   Green           2               |
  | //////////////////////////////////////////+///////////////////////////////////////////// |
  +------------------------------------------------------------------------------------------+

                   Simple Work Flow (not limited to) :
  +<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<+
  |                       +-------------+                                |
  |         +-------------+  READ TAGS  +-------------+                  |
  |         |             +-------------+             |                  |
  |         |                                         |                  |
  |  +------V------+                            +-----V------+           |
  |  | MASTER TAG  |                            | OTHER TAGS |           |
  |  +------+------+                            +-----+------+           |
  |         |                                         |                  |
  |  +------V------+                          +-------V--------+         |
  +--|GRANT ACCESS |                          |                |         |
  |  +------+------+                   +------V------+  +------V------+  |
  |         |                          |  KNOW TAGS  |  |UNKNOWN TAGS |  |
  |  +------V------+                   +------+------+  +------+------+  |
  |  | MASTER TAG  |                          |                |         |
  |  +------+------+                   +------V------+  +------V------+  |
  |         |                          |GRANT ACCESS |  | DENY ACCESS |  |
  |  +------V------+                   +------+------+  +------+------+  |
  |  | UNKNOWN TAG |                          |                |         |
  |  +------+------+                          |                +--------->
  |         |                                 |                          |
  |  +------V------+                          +--------------------------+
  |  |   ADD TO    |
  |  |  USER LIST  |
  |  +-------------+
  |                  +------------+
  +------------------+    EXIT    |
                     +------------+
*/

#include <SPI.h>
#include <MFRC522.h>
#include <LiquidCrystal.h>

#define RST_PIN         9         // Configurable, see typical pin layout above
#define SS_1_PIN        10        // Configurable, take a unused pin, only HIGH/LOW required, must be diffrent to SS 2
#define SS_2_PIN        11        // Configurable, take a unused pin, only HIGH/LOW required, must be diffrent to SS 1

#define NR_OF_READERS   2

byte ssPins[] = {SS_1_PIN, SS_2_PIN};

// Create MFRC522 instance.
MFRC522 mfrc522[NR_OF_READERS];

// initialize the library with the numbers of the interface pins
LiquidCrystal lcd(7, 6, 5, 4, 3, 2);

//Output pins
const int RED_LED = 12;
const int GREEN_LED = 13;
const bool PLS_SIGNAL = 8;

//Variables
boolean cardRecognized = false;
int user_added = 0;

unsigned long nextTimeout = 0;
long interval = 1000;
long previousMillis = 0;
int ledState = LOW;

// Constants representing the states in the state machine
const int S_IDLE = 0;
const int S_COMPARE_TAG = 1;
const int S_ACCESS_GRANTED = 2;
const int S_ACCESS_DENIED = 3;
const int S_ADD_NEW_USER = 4;

//Constants representing the phases for the lights
const int P_GREEN_LIGHT = 0;
const int P_RED_LIGHT = 1;
const int P_IDLE_LIGHTS = 2;

// A variable holding the current state
int currentState = S_IDLE;
int counter = 0;
bool newUserMode = false;
int user = 0;
boolean secondRead = false;

void setup()
{
  SPI.begin();        // Init SPI bus
  Serial.begin(9600); // Initialize serial communications with the PC
  while (!Serial);    // Do nothing if no serial monitor is opened (added for Arduinos based on ATMEGA32U4)

  // Arduino Pin Comfiguration
  pinMode(RED_LED, OUTPUT);
  pinMode(GREEN_LED, OUTPUT);
  pinMode(PLS_SIGNAL, OUTPUT);

  for (uint8_t reader = 0; reader < NR_OF_READERS; reader++) {
    mfrc522[reader].PCD_Init(ssPins[reader], RST_PIN); // Init each MFRC522 card
    Serial.print(F("Reader "));
    Serial.print(reader + 1);
    Serial.print(F(": "));
    mfrc522[reader].PCD_DumpVersionToSerial();
  }

  //Config of the LCD screen
  lcd.begin(16, 2);

  //Config of the LEDS
  setPhase(P_IDLE_LIGHTS);

}


byte ActualUID[4];                         //This will store the ID each time we read a new ID code

byte NewUserUID[4];                            //This will store the ID for the user to add.

byte USER1[4] = {0x77, 0x5A, 0x63, 0x3C} ; //Master ID code Change it for yor tag. First use the READ exampel and check your ID

byte USER2[4] = {0x00, 0x00, 0x00, 0x00} ; //Empty ID of USER2
byte USER3[4] = {0x00, 0x00, 0x00, 0x00} ; //Empty ID of USER3
byte USER4[4] = {0x00, 0x00, 0x00, 0x00} ; //Empty ID of USER4
byte USER5[4] = {0x00, 0x00, 0x00, 0x00} ; //Empty ID of USER5



void loop()
{

  switch (currentState)
  {
    /////////////////// IDLE mode, RFID scanner looking for tags.
    case S_IDLE:
      setPhase(P_IDLE_LIGHTS);
      idleLCDState();
      getCardInfo();
      if(getCardInfo())
      {
      changeStateTo(S_COMPARE_TAG);
      }
      break;

    /////////////////// Checks if the scanned tag is stored. Or else access denied
    case S_COMPARE_TAG:
      compareTags();
      Serial.println(cardRecognized);
      if (cardRecognized)
      {
        startTimer(10000);
        cardRecognized = false;
        changeStateTo(S_ACCESS_GRANTED);
        
      }
      else if (!cardRecognized)
      {
        startTimer(3000);
        printAccessDenied();
        changeStateTo(S_ACCESS_DENIED);
      }
      break;


    /////////////////// access granted to user.
    case S_ACCESS_GRANTED:
      if (newUserMode == false) {
        printAccessGranted();
        Serial.println("lul");
      }
      else if (timerHasExpired())
      {
        changeStateTo(S_IDLE);
        Serial.println("start");
      } 
      else if (!timerHasExpired() && newUserMode)
      {
        Serial.println("stop");
        startTimer(6000);
        compareTags();
        getNewUserInfo();
        if (!cardRecognized)
        {
        addUser();
        countdown();
        changeStateTo(S_ADD_NEW_USER);
        }
      }
      break;

    ////////////////// Scanned tag where not recognized, access denied to user.
    case S_ACCESS_DENIED:
      if (timerHasExpired())
      {
        changeStateTo(S_IDLE);
      }
      break;

    /////////////////// Mastercard scanned twice, new user mode activated.
    case S_ADD_NEW_USER:
      if (timerHasExpired())
      {
        changeStateTo(S_IDLE);
        counter = 0;
      }
      break;
  }

}
////////////////////////////////////////////////END OF LOOP
  /**
   * Sets the lights according to the phase provided.
   * Phase 1: Green led only, Access granted
   * Phase 2: Red led only, Access denied
   * Phase 3: Both leds off, idle mode
   * 
   * @param phase The phase to use to set the pattern of the LED's accordingly
   */
void setPhase (int phase)
{
  switch (phase)
  {
    case P_GREEN_LIGHT: // ACCESS GRANTED
      digitalWrite(RED_LED, LOW);      // Make sure red LED is off
      digitalWrite(GREEN_LED, HIGH);   // Make sure green LED is on
      digitalWrite(PLS_SIGNAL, HIGH);  // Make sure PLS signal is high
      break;

    case P_RED_LIGHT: // ACCESS DENIED
      digitalWrite(RED_LED, HIGH);     // Make sure red LED is on
      digitalWrite(GREEN_LED, LOW);    // Make sure green LED is off
      digitalWrite(PLS_SIGNAL, LOW);   // Make sure PLS signal is low
      break;

    case P_IDLE_LIGHTS: // IDLE MODE
      digitalWrite(RED_LED, LOW);      // Make sure red LED is off
      digitalWrite(GREEN_LED, LOW);    // Make sure green LED is off
      digitalWrite(PLS_SIGNAL, LOW);   // Make sure PLS signal is low
      break;

    default:
      digitalWrite(RED_LED, HIGH);     // Make sure red LED is on
      digitalWrite(GREEN_LED, HIGH);   // Make sure green LED is on
      digitalWrite(PLS_SIGNAL, LOW);   // Make sure PLS signal is low
      break;

  }
}

/**
   Prints the state to Serial Monitor as a text, based
   on the state-constant provided as the parameter state

   @param state The state to print the tekst-representation for
*/
void printState(int state)
{
  switch (state)
  {
    case S_IDLE:
      Serial.print("S_IDLE");
      break;

    case S_COMPARE_TAG:
      Serial.print("S_COMPARE_TAG");
      break;

    case S_ACCESS_GRANTED:
      Serial.print("S_ACCESS_GRANTED");
      break;

    case S_ACCESS_DENIED:
      Serial.print("S_ACCESS_DENIED");
      break;

    case S_ADD_NEW_USER:
      Serial.print("S_ADD_NEW_USER");
      break;

    default:
      Serial.print("!!UNKNOWN!!");
      break;
  }
}

/**
  Change the state of the statemachine to the new state
  given by the parameter newState

  @param newState The new state to set the statemachine to
*/
void changeStateTo(int newState)
{
  // At this point, we now what the current state is (the value
  // of the global variable currentState), and we know what the
  // next state is going to be, given by the parameter newState.
  // By using the printState()-funksjon, we can now print the
  // full debug-message:
  Serial.print("State changed from ");
  printState(currentState);
  Serial.print(" to ");
  printState(newState);
  Serial.println(); // To add a new line feed

  // And finally, set the current state to the new state
  currentState = newState;
}

//////////////////////////////// Print idle LCD state  /////////////////////////////////
void idleLCDState()
{
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Scan card to be");
  lcd.setCursor(0, 1);
  lcd.print("granted ACCESS!");
}

//////////////// Compare the 4 bytes of the users and the received ID //////////////////
boolean compareArray(byte array1[], byte array2[])
{
  if (array1[0] != array2[0])return (false);
  if (array1[1] != array2[1])return (false);
  if (array1[2] != array2[2])return (false);
  if (array1[3] != array2[3])return (false);
  return (true);
}

////////////////////////////// Print added user to LCD /////////////////////////////////
void printAddedUser(int user, byte USER[])
{
  USER[0] = NewUserUID[0];
  USER[1] = NewUserUID[1];
  USER[2] = NewUserUID[2];
  USER[3] = NewUserUID[3];
  user_added = user_added + 1;
  newUserMode = false;
  lcd.setCursor(0, 0);
  lcd.print("New user stored ");
  lcd.setCursor(0, 1);
  lcd.print("   as  USER ");
  lcd.print(user);
  delay(3000);
}

//////////////////////////////// Print Access to LCD  //////////////////////////////////
void printAccessGranted()
{
  if (user == 1)
  {
    lcd.setCursor(0, 0);
    lcd.print(" Access granted ");
    lcd.setCursor(0, 1);
    lcd.print("  MASTER  USER  ");
    Serial.println("Last use: MASTER CARD");
    setPhase (P_GREEN_LIGHT);
    delay(2000);
    setPhase (P_IDLE_LIGHTS);
    lcd.setCursor(0, 0);
    lcd.print("Scan MASTERCARD");
    lcd.setCursor(0, 1);
    lcd.print("to add new ID  6");
    newUserMode = true;
  }
  else
  {
    lcd.clear();
    lcd.setCursor(1, 0);
    lcd.print("Access granted");
    lcd.setCursor(5, 1);
    lcd.print("USER ");
    lcd.print(user);
    setPhase (P_GREEN_LIGHT);
    delay(2000);
    setPhase (P_IDLE_LIGHTS);
  }
}

///////////////////////////////////// FUNCTIONS ////////////////////////////////////////
/**
  Helper routine to dump a byte array as hex values to Serial.
*/
void dump_byte_array(byte * buffer, byte bufferSize) {
  for (byte i = 0; i < bufferSize; i++) {
    Serial.print(buffer[i] < 0x10 ? " 0" : " ");
    Serial.print(buffer[i], HEX);
  }
}


void addUser()
{
  //Compare the read ID and the stored USERS
  if (user_added == 4) // FULL
  {
    lcd.setCursor(0, 0);
    lcd.print("  User list is  ");
    lcd.setCursor(0, 1);
    lcd.print("      FULL      ");
    idleLCDState();
    Serial.println("Last action: User list is full.");
  }

  if (user_added == 3) // 5
  {
    printAddedUser(5, USER5);
  }

  if (user_added == 2) // 4
  {
    printAddedUser(4, USER4);

  }

  if (user_added == 1) //3
  {
    printAddedUser(3, USER3);
  }

  if (user_added == 0) // 2
  {
    printAddedUser(2, USER2);
  }
}

void countdown()
{
  if (counter > 300)
  {
    lcd.setCursor(0, 0);
    lcd.print("New ID  canceled");
    lcd.setCursor(0, 1);
    lcd.print("                ");
    delay(3000);
  }

  if (counter == 50)
  {
    lcd.setCursor(15, 1);
    lcd.print("5");
  }

  if (counter == 100)
  {
    lcd.setCursor(15, 1);
    lcd.print("4");
  }

  if (counter == 150)
  {
    lcd.setCursor(15, 1);
    lcd.print("3");
  }

  if (counter == 200)
  {
    lcd.setCursor(15, 1);
    lcd.print("2");
  }

  if (counter == 250)
  {
    lcd.setCursor(15, 1);
    lcd.print("1");
  }
  counter = counter + 1;
  delay(10);
}

void printAccessDenied()
{
  lcd.setCursor(0, 0);
  lcd.print(" Access  denied ");
  lcd.setCursor(0, 1);
  lcd.print("   UNKNOWN ID   ");
  cardRecognized = false;
  Serial.println("Last use: UNKNOWN ID, Access DENIED");
  setPhase(P_RED_LIGHT);
  delay(3000);
}

/**
   Checks if the timer has expired. If the timer has expired,
   true is returned. If the timer has not yet expired,
   false is returned.

   @return true if timer has expired, false if not
*/
boolean timerHasExpired()
{
  boolean hasExpired = false;
  if (millis() > nextTimeout)
  {
    hasExpired = true;
  }
  else
  {
    hasExpired = false;
  }
  return hasExpired;
}

/**
   Starts the timer and set the timer to expire after the
   number of milliseconds given by the parameter duration.

   @param duration The number of milliseconds until the timer expires.
*/
void startTimer(unsigned long duration)
{
  nextTimeout = millis() + duration;
}

boolean getCardInfo()
{
  for (uint8_t reader = 0; reader < NR_OF_READERS; reader++)
    // Check if there are any new ID card in front of the sensor
    if ( mfrc522[reader].PICC_IsNewCardPresent() && mfrc522[reader].PICC_ReadCardSerial())
    {
      // We store the read ID into 4 bytes with a for loop
      for (byte i = 0; i < mfrc522[reader].uid.size; i++)
      {
        ActualUID[i] = mfrc522[reader].uid.uidByte[i];
      }
      return true;
      mfrc522[reader].PICC_HaltA();
    }
}

void getNewUserInfo()
{ 
  for (uint8_t reader = 0; reader < NR_OF_READERS; reader++)
    // Check if there are any new ID card in front of the sensor
    if ( mfrc522[reader].PICC_IsNewCardPresent() && mfrc522[reader].PICC_ReadCardSerial())
    {
      // We store the read ID into 4 bytes with a for loop
      for (byte i = 0; i < mfrc522[reader].uid.size; i++)
      {
        NewUserUID [i] = mfrc522[reader].uid.uidByte[i];
      }
      
    }
    
}

void resetScan ()
{
  for (uint8_t reader = 0; reader < NR_OF_READERS; reader++)
    // Check if there are any new ID card in front of the sensor
  if ( ! mfrc522[reader].PICC_IsNewCardPresent() && ! mfrc522[reader].PICC_ReadCardSerial()) { //If a new Tag placed to RFID reader continue
    return 0;
  } 
}

bool compareTags()
{
  cardRecognized = false;
  if (compareArray(ActualUID, USER1))
  {
    user = 1;
    cardRecognized = true;
    Serial.println("Test - ActualUID = MasterCard");
  }
  if (compareArray(ActualUID, USER2))
  {
    user = 2;
    cardRecognized = true;
    Serial.println("Test - USER2");
  }
  if (compareArray(ActualUID, USER3))
  {
    user = 3;
    cardRecognized = true;
    Serial.println("Test -USER3");
  }
  if (compareArray(ActualUID, USER4))
  {
    user = 4;
    cardRecognized = true;
    Serial.println("Test - USER4");
  }
  if (compareArray(ActualUID, USER5))
  {
    user = 5;
    cardRecognized = true;
    Serial.println("Test - USER5");
  }
  return cardRecognized;
}
