/*
 * PWM_test_code_1126_refactored_lcd.c
 *
 * Created: 2025-11-26
 * Author : cchhy
 */ 

#define F_CPU 16000000UL
#include <avr/io.h>
#include <util/delay.h>

#include "lcd.h"   // LCD 함수 선언

// PE4 PWM, PE5 IN1, PE6 IN2 사용. Timer3 사용 + PB3:0 포트의 액티브-로우 버튼 사용
// L298N 기준으로 PE4는 ENA, PE5는 IN1, PE6은 IN2에 연결

// ===== 전역 상태 변수 =====
uint8_t  mode = 0;      // 0: stop, 1: slow, 2: middle, 3: fast
uint16_t duty = 0;      // PWM duty (0 ~ 1023)

// LCD 업데이트용 이전 상태 기록
uint8_t prev_mode = 255;  // 초기값: 존재할 수 없는 값으로 설정

// 포트 입출력 설정, PWM 제어 레지스터 초기화
void GPIO_PWM_init(void)
{
    // --- 버튼 (PB3:0) 풀업 입력 ---
    PORTB |= (1<<PB0)|(1<<PB1)|(1<<PB2)|(1<<PB3);   // 내부 풀업 ON
    DDRB  &= ~((1<<PB0)|(1<<PB1)|(1<<PB2)|(1<<PB3)); // 입력

    // --- 모터 제어 핀 설정 (PE4,5,6) ---
    // 초기 값: IN1=HIGH, IN2=LOW → 정방향, ENA(PE4)는 PWM으로 제어
    PORTE &= ~((1<<PE4)|(1<<PE6));  // PE4, PE6 LOW
    PORTE |=  (1<<PE5);             // PE5 HIGH
    DDRE  |=  (1<<PE4)|(1<<PE5)|(1<<PE6); // 출력으로 설정

    // --- Timer3 설정: Fast PWM 10bit, OC3B 비반전 출력 ---
    // WGM3[3:0] = 0b0111 → Fast PWM 10bit (TOP = 1023)
    TCCR3A |= (1<<WGM30)|(1<<WGM31)|(1<<COM3B1); // COM3B1=1 → OC3B 비반전
    TCCR3B |= (1<<CS30)|(1<<CS31)|(1<<WGM32);    // 분주비 64, WGM32=1
    // f_PWM ≈ 16MHz / (64 * 1024) ≈ 244Hz
}

// LCD 초기 화면 출력
void LCD_DisplayInit(void)
{
    LCDCommand(ALLCLR);
    LCDCommand(HOME);

    // 1줄: "Current State"
    LCDMove(0, 0);          // 1행 0번째 칸
    LCDPuts("Current State");

    // 2줄: "Level : 0"
    LCDMove(1, 0);          // 2행 0번째 칸
    LCDPuts("Level : 0");

    prev_mode = mode;       // 현재 mode(0)를 이전 상태로 기록
}

// mode가 바뀌었을 때만 숫자 부분 갱신
void LCD_UpdateIfChanged(void)
{
    if (mode != prev_mode)
    {
        // "Level : " 이 8글자니까, 그 뒤 칸(1행 8번 위치)에 숫자 출력
        LCDMove(1, 8);                // 2행, 8번째 칸
        LCDPutchar('0' + mode);       // mode는 0~3이므로 한 자리 숫자

        prev_mode = mode;
    }
}

// 버튼 입력을 읽어 "모드"를 변경하는 함수
// 버튼이 눌렸을 때만 mode를 바꾸고, 안 눌렸으면 그대로 유지
void condition(void)
{
    uint8_t pins = PINB;

    // 액티브-로우 버튼: 눌리면 0이므로 !(pins & (1<<PBx))가 true
    if (!(pins & (1<<PB0))) {
        mode = 0;   // STOP
    }
    else if (!(pins & (1<<PB1))) {
        mode = 1;   // SLOW
    }
    else if (!(pins & (1<<PB2))) {
        mode = 2;   // MIDDLE
    }
    else if (!(pins & (1<<PB3))) {
        mode = 3;   // FAST
    }
    // 어떤 버튼도 안 눌렸으면 mode를 바꾸지 않음 → 이전 상태 유지
}

// 모터 실질 구동 함수: 현재 mode에 따라 duty를 결정해서 OCR3B에 반영
void Motor_Active(void)
{
    switch (mode)
    {
        case 0: // STOP
            duty = 530;
            break;

        case 1: // SLOW
            duty = 800;
            break;

        case 2: // MIDDLE
            duty = 1023;
            break;

        case 3: // FAST
            duty = 900;
            break;

        default:
            duty = 0; // 방어 코드
            break;
    }

    OCR3B = duty; // 듀티비를 마지막에 바꾸어서, 다음 입력이 들어올 때까지 유지
}

int main(void)
{
    MCU_Init();       // LCD용 외부 메모리 / 보드 초기화
    GPIO_PWM_init();  // 모터/버튼용 GPIO + Timer3 설정
    LCDInit();        // LCD 컨트롤러 초기화
    LCD_DisplayInit();// "Current State" / "Level : 0" 출력

    while (1) // 버튼 감지 + LCD 갱신 + 모터 동작 반복
    {
        condition();        // 버튼 눌림이 있으면 mode 변경
        LCD_UpdateIfChanged(); // mode가 바뀌면 LCD 숫자 먼저 갱신
        Motor_Active();     // 그 다음 모터 PWM 반영
        _delay_ms(20);      // 소프트 디바운싱 겸 폴링 속도 조절
    }
}
