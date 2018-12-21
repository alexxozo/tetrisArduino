#include <EEPROM.h>  // for highscores 100.000 write/erase cycles
#include <LiquidCrystal.h>
#include "LedControl.h"
#include "digits.h"

// Game Logic
#define ROWS 8
#define COLUMNS 8
#define BLOCK_WIDTH 2
#define BLOCK_HEIGHT 2
#define BUTTON_LEFT 2
#define BUTTON_MID 3
#define BUTTON_RIGHT 4
#define BUTTON_BIG 5
#define NO_OF_BUTTONS 4
#define DEBOUNCE_DELAY 50
#define EASY_DELAY 1000
#define MEDIUM_DELAY 500
#define HARD_DELAY 200

// Matrix
#define DIN 12
#define CLK 11
#define LOAD 10
#define NO_DRIVER 1

// LCD display
#define RS A4
#define EN A5
#define D4 A3
#define D5 A2
#define D6 A1
#define D7 A0
#define BACKLIGHT 6;

// clang-format off
enum Rotation { UP = 1, RIGHT = 2, DOWN = 3, LEFT = 4 };
enum State { MENU, PLAY, LOCKED, WAIT };

State gameState = MENU;

uint8_t eepromAdress = 0;

int x, y, blockIndex = 0, currentRotation = 1, frameDelay = EASY_DELAY,
          currentDelay = EASY_DELAY, score = 0, highScore = 0, 
          buttonsPins[NO_OF_BUTTONS] = {BUTTON_LEFT, BUTTON_MID, BUTTON_RIGHT, BUTTON_BIG};
bool buttonState[NO_OF_BUTTONS] = {false},
     lastButtonState[NO_OF_BUTTONS] = {true},
     buttonFlag[NO_OF_BUTTONS] = {false}, fallingBlock = true, firstTime = true;
unsigned long lastDebounceTime[NO_OF_BUTTONS] = {0}, prevTimeGame = 0,
              prevTimeInput = 0;


LedControl lc = LedControl(DIN, CLK, LOAD, NO_DRIVER);  // DIN, CLK, LOAD, NO. DRIVER

LiquidCrystal lcd(RS, EN, D4, D5, D6, D7);


bool matrix[8][8] = {
  {0, 0, 0, 0, 0, 0, 0, 0},
  {0, 0, 0, 0, 0, 0, 0, 0},
  {0, 0, 0, 0, 0, 0, 0, 0},
  {0, 0, 0, 0, 0, 0, 0, 0},
  {0, 0, 0, 0, 0, 0, 0, 0},
  {0, 0, 0, 0, 0, 0, 0, 0},
  {0, 0, 0, 0, 0, 0, 0, 0},
  {0, 0, 0, 0, 0, 0, 0, 0}
};
bool collisionMatrix[8][8] = {
  {0, 0, 0, 0, 0, 0, 0, 0},
  {0, 0, 0, 0, 0, 0, 0, 0},
  {0, 0, 0, 0, 0, 0, 0, 0},
  {0, 0, 0, 0, 0, 0, 0, 0},
  {0, 0, 0, 0, 0, 0, 0, 0},
  {0, 0, 0, 0, 0, 0, 0, 0},
  {0, 0, 0, 0, 0, 0, 0, 0},
  {0, 0, 0, 0, 0, 0, 0, 0}
};
bool blocks[3][4] = {{1, 1, 1, 1}, {1, 1, 0, 0}, {1, 1, 0, 1}};
bool currentBlock[4];

// Copies one array to another
void appendBlockConfig(bool a[BLOCK_WIDTH * BLOCK_HEIGHT], bool b[BLOCK_WIDTH * BLOCK_HEIGHT]) {
  for (int i = 0; i < BLOCK_WIDTH * BLOCK_HEIGHT; i++)
    a[i] = b[i];
}

// Draws a block at row 'y' and column 'x' with the rotation 'r'
void drawBlock(bool block[BLOCK_WIDTH * BLOCK_HEIGHT], int x, int y, Rotation r) {
  if (r == UP) {
    for (int i = y; i < y + 2; i++)
      for (int j = x; j < x + 2; j++)
        matrix[i][j] = currentBlock[(i - y) * 2 + (j - x)];
  } else if (r == RIGHT) {
    for (int i = y; i < y + 2; i++)
      for (int j = x; j < x + 2; j++)
        matrix[i][j] = currentBlock[2 + (i - y) - (j - x) * 2];
  } else if (r == DOWN) {
    for (int i = y; i < y + 2; i++)
      for (int j = x; j < x + 2; j++)
        matrix[i][j] = currentBlock[3 - (i - y) * 2 - (j - x)];
  } else if (r == LEFT) {
    for (int i = y; i < y + 2; i++)
      for (int j = x; j < x + 2; j++)
        matrix[i][j] = currentBlock[1 - (i - y) + 2 * (j - x)];
  }
}

// Draws the game matrix: the matrix containing the current block + the matrix with all the previous blocks
void drawGameMatrix() {
  for (int i = 0; i < ROWS; i++)
    for (int j = 0; j < COLUMNS; j++)
      lc.setLed(0, i, j, matrix[i][j] || collisionMatrix[i][j]);
}

// Clears the matrix which contains the current block
void clearMatrix() {
  for (int i = 0; i < ROWS; i++)
    for (int j = 0; j < COLUMNS; j++) matrix[i][j] = 0;
}

// Clears the game matrix
void clearGameMatrix() {
  for (int i = 0; i < ROWS; i++)
    for (int j = 0; j < COLUMNS; j++) collisionMatrix[i][j] = 0;
}

// Draws the matrix using an array of bytes
void drawMatrixBinary(byte row[]) {
  for (int i = 0; i < ROWS; i++) 
    lc.setRow(0, i, row[i]);
}

// Prints matrix for debug
void printMatrix(bool matrix[ROWS][COLUMNS]) {
  for (int i = 0; i < ROWS; i++) {
    for (int j = 0; j < COLUMNS; j++) {
      Serial.print(matrix[i][j]);
      Serial.print(" ");
    }
    Serial.println(" ");
  }
  Serial.println("-----------------");
}

// Combines 2 matrixes such as the result contains all the elements from these matrixes
void matrixUnion(bool a[ROWS][COLUMNS], bool b[ROWS][COLUMNS], bool (&result)[ROWS][COLUMNS]) {
  for (int i = 0; i < ROWS; i++)
    for (int j = 0; j < COLUMNS; j++) result[i][j] = (a[i][j] || b[i][j]);
}

// If the current block hits the bottom of the game matrix OR another block, returns true
bool collisionOccured() {
  for (int i = 0; i < ROWS; i++)
    for (int j = 0; j < COLUMNS; j++)
      if (matrix[ROWS - 1][j] == 1 ||
          (collisionMatrix[i][j] == 1 &&
           collisionMatrix[i][j] == matrix[i - 1][j])) {
        matrixUnion(matrix, collisionMatrix, collisionMatrix);
        // printMatrix(collisionMatrix);
        return true;
      }
  return false;
}

// If there is a block touching the top of the game matrix, then we know the game is over
bool checkGameOver() {
  for (int i = 0; i < COLUMNS; i++)
    if (collisionMatrix[0][i] == 1) return true;
  return false;
}

// We store the row for the last edge of our block
//     0  1
//     ----
// 0 | 1  0
// 1 | 1  1 
// Say this is our block then our array would be [0, 1]
// Then if we want to go to the right we check if the rows next to our rows arrays are empty
// If [0 + 1, 1 + 1] are empty then it's a valid move
bool isValidMove(int x, int y, int direction) {
  // Going to the RIGHT
  if (direction == 1) {
    int xCoverage[BLOCK_HEIGHT];
    for (int row = y; row < y + BLOCK_HEIGHT; row++) {
      for (int column = x + (BLOCK_WIDTH - 1); column >= x; column--) {
        if (matrix[row][column] == 1) {
          xCoverage[row - y] = column;
          break;
        }
        xCoverage[row - y] = -1;
      }
    }
    for (int i = 0; i < BLOCK_HEIGHT; i++) {
      int columnToVerify = xCoverage[i];
      if (columnToVerify != -1) {
        if (columnToVerify + 1 == COLUMNS) return false;
        if (collisionMatrix[y + i][columnToVerify + 1] == 1) return false;
      }
    }
  } else {
    // Going to the LEFT
    int xCoverage[BLOCK_HEIGHT];
    for (int row = y; row < y + BLOCK_HEIGHT; row++) {
      for (int column = x; column <= x + (BLOCK_WIDTH - 1); column++) {
        if (matrix[row][column] == 1) {
          xCoverage[row - y] = column;
          break;
        }
        xCoverage[row - y] = -1;
      }
    }
    for (int i = 0; i < BLOCK_HEIGHT; i++) {
      int columnToVerify = xCoverage[i];
      if (columnToVerify != -1) {
        if (columnToVerify - 1 == -1) return false;
        if (collisionMatrix[y + i][columnToVerify - 1] == 1) return false;
      }
    }
  }
  return true;
}

// Checks if a row is complete
bool isCompleteRow(int row) {
  for (int i = 0; i < COLUMNS; i++)
    if (collisionMatrix[row][i] != 1) return false;
  return true;
}

// Move all the game matrix down by one
void eliminateRow(int row) {
  for (int i = row; i > 0; i--)
    for (int j = 0; j < COLUMNS; j++)
      collisionMatrix[i][j] = collisionMatrix[i - 1][j];
  for (int j = 0; j < COLUMNS; j++) collisionMatrix[0][j] = 0;
}

// Chack for any complete row and eliminates it
bool scanForCompleteRows() {
  bool flag = false;
  for (int i = 0; i < ROWS; i++)
    if (isCompleteRow(i)) {
      score += 10;
      lc.setRow(0, i, B00000000);
      delay(50);
      lc.setRow(0, i, B11111111);
      delay(50);
      lc.setRow(0, i, B00000000);
      delay(50);
      lc.setRow(0, i, B11111111);
      delay(50);
      eliminateRow(i);
      flag = true;
    }
  return flag;
}

// A helper function - which will trigger at the delayTime interval
bool delayFunction(unsigned long &prevTime, int delayTime) {
  if (millis() - prevTime >= delayTime) {
    prevTime = millis();
    return true;
  } else
    return false;
}

// Reads all the buttons inputs
void readButtons() {
  for (int i = 0; i < NO_OF_BUTTONS; i++) {
    int reading = digitalRead(buttonsPins[i]);

    if (reading != lastButtonState[i]) {
      lastDebounceTime[i] = millis();
    }

    if (delayFunction(lastDebounceTime[i], DEBOUNCE_DELAY)) {
      if (reading != buttonState[i]) {
        buttonState[i] = reading;

        if (buttonState[i] == 0) {
          buttonFlag[i] = !buttonFlag[i];
        }
      } else {
        if (buttonState[i] == 0) {
          buttonState[i] = 1;
        }
      }
    }

    lastButtonState[i] = reading;
  }
}

void setup() {
  // the zero refers to the MAX7219 number, it is zero for 1 chip
  lc.shutdown(0, false);  // turn off power saving, enables display
  lc.setIntensity(0, 2);  // sets brightness (0~15 possible values)
  lc.clearDisplay(0);     // clear screen

  // LCD Setup for analog pins
  // *needed only if you use analog pins
  pinMode(D7, OUTPUT);
  pinMode(D6, OUTPUT);
  pinMode(D5, OUTPUT);
  pinMode(D4, OUTPUT);
  pinMode(EN, OUTPUT);
  pinMode(RS, OUTPUT);
  // LCD Backlight
  pinMode(6, OUTPUT);
  analogWrite(6, 20); 

  // set up the LCD's number of columns and rows:
  lcd.begin(16, 2);
  lcd.print("-----TETRIX-----");
  lcd.setCursor(0, 1);
  lcd.print("Press any key...");

  // Can't be used with lcd
  //Serial.begin(9600); 

  // Buttons
  pinMode(BUTTON_LEFT, INPUT_PULLUP);
  pinMode(BUTTON_MID, INPUT_PULLUP);
  pinMode(BUTTON_RIGHT, INPUT_PULLUP);
  pinMode(BUTTON_BIG, INPUT_PULLUP);

  // Setting up the initial states
  appendBlockConfig(currentBlock, blocks[1]);
  x = COLUMNS / 2 - 1;
  y = 0;
  gameState = LOCKED;
  
  randomSeed(analogRead(0)); // used for generating random numbers
}

void loop() {

  if(gameState == LOCKED) {
    readButtons();
    // If any button is pressed go to MENU
    for (int i = 0; i < NO_OF_BUTTONS; i++)
      if(buttonFlag[i] == true) {
        buttonFlag[i] = false;
        gameState = MENU; 
      }
  } else if (gameState == MENU) {
    readButtons();
    // If it's the first time in the menu show the difficulties
    if(firstTime) {
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Difficulty:");
      lcd.setCursor(0, 1);
      lcd.print("EASY-MEDIUM-HARD");
      firstTime = false;
    }
    // Check to see which difficulty has been selected
    for (int i = 0; i < NO_OF_BUTTONS; i++) {
          if(buttonFlag[i] == true) {
            buttonFlag[i] = false;
            lcd.clear();
            lcd.setCursor(0, 1);
            switch (i) {
              case 0:
                lcd.print("------EASY------");
                delay(1000);
                currentDelay = EASY_DELAY;
                break;
              case 1: 
                lcd.print("-----MEDIUM-----");
                delay(1000);
                currentDelay = MEDIUM_DELAY;
                break;
              case 2:
                lcd.print("------HARD------");
                delay(1000);
                currentDelay = HARD_DELAY;
                break;
              }
            // Just an animation for showing the selected difficulty
            delay(1000);
            lcd.clear();
            lcd.setCursor(0, 0);
            // Then start the game
            highScore = EEPROM.read(eepromAdress);
            lcd.print(String("Highscore: ") + String(highScore));
            lcd.setCursor(0, 1);
            lcd.print(String("Score: ") + String(score));
            // After this countdown            
            drawMatrixBinary(digits[2]);
            delay(1000);
            drawMatrixBinary(digits[1]);
            delay(1000);
            drawMatrixBinary(digits[0]);
            delay(1000);
            // Initial states for the game
            frameDelay = currentDelay;
            gameState = PLAY;
            score = 0;
          }
    }
  } else if (gameState == PLAY) {
    // We read the state of the buttons and move our currentBlock at a 20ms interval
    if (delayFunction(prevTimeInput, 20)) {
      readButtons();
      // This is the right moving button
      if (buttonFlag[2] == true && isValidMove(x, y, 1)) {
        x++;
        buttonFlag[2] = false;
      // This is the left moving button
      } else if (buttonFlag[0] == true && isValidMove(x, y, -1)) {
        x--;
        buttonFlag[0] = false;
        // This is the button for changing the rotation of the currentBlock
      } else if (buttonFlag[1] == true) {
        buttonFlag[1] = false;
        // If block is 'line' we need to jump over 2 rotations
        // a line can only rotate in 2 ways : | and  _
        // else we need to rotate the block in all 4 rotations
        if (blockIndex == 1)
          currentRotation = (currentRotation + 1 == 3) ? currentRotation = 1 : currentRotation + 1;
        else
          currentRotation = (currentRotation + 1 == 5) ? currentRotation = 1 : currentRotation + 1;
        // This is the button for changing the falling speed of the currentBlock
      } else if (buttonFlag[3] == true) {
        buttonFlag[3] = false;
        frameDelay = 100;
      }
    }

    // We move the currentBlock vertically at an interval equal to frameDelay, based on difficulty
    if (delayFunction(prevTimeGame, frameDelay)) {
      // If the block is currently falling
      if (fallingBlock) {
        y++;
        // If we encountered any collision wither with the ground OR any previous blocks
        if (collisionOccured()) {
          //printMatrix(matrix); // DEBUG
          fallingBlock = false;
          // Reseting the variables for the next block, and choosing the next block randomly
          x = 3;
          y = 0;
          frameDelay = EASY_DELAY;
          currentRotation = 1;
          blockIndex = random(0, 3);
          appendBlockConfig(currentBlock, blocks[blockIndex]);
        }
        // If the block is no longer falling
      } else {
        // We check if there is any complete row we could erase and change the score
        if(scanForCompleteRows()) {
          lcd.clear();
          lcd.setCursor(0, 1);
          lcd.print(String("Score: ") + String(score));
        }
        // Preparing for the new block
        clearMatrix();
        fallingBlock = true;
        if (checkGameOver()) {
          // If the game is over then we need to set a new highscore and wait for user input to restart the game
          EEPROM.write(eepromAdress, score);
          clearGameMatrix();
          gameState = WAIT;
          firstTime = true;
        }
      }
    }

    // We draw the game matrix every loop
    clearMatrix();
    drawBlock(currentBlock, x, y, currentRotation);
    drawGameMatrix();
  } else if(gameState == WAIT) {
      // Waiting for any user input to go to the MENU
      readButtons();
      for (int i = 0; i < NO_OF_BUTTONS; i++)
        if(buttonFlag[i] == true) {
          buttonFlag[i] = false;
          gameState = MENU; 
        }
  }

  
}
