/*
 * HX711 + PWM + LCD 통합 모듈 예제 (리팩토링 버전)
 *
 * - HX711 raw 값을 읽어서 LCD 2줄로 표시
 *   1줄: "Speed Level X"
 *   2줄: "D : raw"
 *
 * - HX711은 ATmega128의 PORTD 사용
 *   DOUT : PD0
 *   SCK  : PD1
 *
 * - 현재는 HX711 보정 전이므로, PB2~PB0 버튼으로 상태 플래그(0~3)를 강제로 갱신
 *   (나중에 HX_711_Update() 내부에서 버튼 로직을 제거하고,
 *    raw 값으로 상태 플래그를 갱신하면 인터페이스는 그대로 유지됨)
 */

#ifndef F_CPU
#define F_CPU 16000000UL
#endif

#include <avr/io.h>
#include <util/delay.h>
#include <stdlib.h>
#include "lcd.h"   // MCU_Init(), LCDInit(), LCDCommand(), LCDMove(), LCDPuts() 등

// ===================== 공통 전역 변수 =====================

// HX711에서 읽은 24비트 raw 값 (보정 + 필터 후 값으로 사용)
volatile unsigned long g_hx711_raw = 0;

// 모터/속도 제어용 상태 플래그 (0~3)
volatile uint8_t g_speed_level = 0;   // 0: stop, 1/2/3: 점점 빠르게

// 모터 PWM duty (0~1023, Fast PWM 10bit)
volatile uint16_t g_duty = 0;

// ===================== HX711 필터 / 임계값 파라미터 =====================

// 전원 켰을 때, 아무것도 올려놓지 않은 상태에서 자동 제로(tare)를 위한 오프셋
// static unsigned long g_hx711_offset = 0;

// 1차 저역 필터(지수 이동 평균) 계수: out = (9/10)*out + (1/10)*in
#define HX_FILTER_ALPHA_NUM   9
#define HX_FILTER_ALPHA_DEN   10

// 예시: 빈 상태 ≈ 8,300,000, 컵 ≈ 8,305,000일 때
#define LV1_ON   8301000UL
#define LV2_ON   8303000UL
#define LV3_ON   8305000UL

#define LV1_OFF  8300500UL
#define LV2_OFF  8302500UL
#define LV3_OFF  8304500UL


// ===================== HX711 관련 (PORTD) =====================
// DOUT : PD0
// SCK  : PD1

#define HX_DOUT_PD   PD0
#define HX_SCK_PD    PD1


// HX711 ready 체크 (DOUT LOW = ready)
static uint8_t HX711_isReady(void)
{
    return ((PIND & (1 << HX_DOUT_PD)) == 0);
}

// 24비트 raw 읽기 (unsigned long, 하위 24비트만 사용)
static unsigned long HX711_ReadRaw24(void)
{
    unsigned long count = 0;

    // ready 대기
    while (!HX711_isReady())
    {
        // busy wait
    }

    // 24비트 읽기 (MSB first)
    for (uint8_t i = 0; i < 24; i++)
    {
        // SCK HIGH
        PORTD |= (1 << HX_SCK_PD);
        _delay_us(1);

        // 왼쪽으로 시프트 후 DOUT 비트 반영
        count <<= 1;
        if (PIND & (1 << HX_DOUT_PD))
        {
            count++;
        }

        // SCK LOW
        PORTD &= ~(1 << HX_SCK_PD);
        _delay_us(1);
    }

    // 채널 A, gain 128 설정용 추가 클럭 1번
    PORTD |= (1 << HX_SCK_PD);
    _delay_us(1);
    PORTD &= ~(1 << HX_SCK_PD);
    _delay_us(1);

    // 상위 8비트는 무시하고, 하위 24비트만 사용
    count &= 0xFFFFFFUL;

    return count;
}

// (필요시 사용) 24비트 raw → signed long 변환
/* static long HX711_Raw24_to_Signed(unsigned long raw)
{
    raw &= 0xFFFFFFUL;

    if (raw & 0x800000UL)       // 24bit MSB가 1이면
    {
        raw |= 0xFF000000UL;    // 상위 비트 1로 채움 (sign extend)
    }
    return (long)raw;
} */

// HX711 핀 방향 및 초기 상태 설정
void HX_711_Init(void)
{
	// DOUT 입력, SCK 출력
	DDRD &= ~(1 << HX_DOUT_PD);   // 입력
	DDRD |=  (1 << HX_SCK_PD);    // 출력

	// DOUT 풀업 OFF, SCK idle LOW
	PORTD &= ~(1 << HX_DOUT_PD);
	PORTD &= ~(1 << HX_SCK_PD);

	// 여기서는 offset 보정 안 함
	g_hx711_raw = HX711_ReadRaw24();  // 또는 0으로 초기화해도 됨
}



// HX711 구동: raw 값 읽어서 g_hx711_raw에 저장
// (필요하면 여기에서 다회 샘플링 평균 등 필터를 추가할 수 있음)
void HX_711_Active(void)
{
	const uint8_t N = 5;
	unsigned long acc = 0;

	// 1) N회 샘플링해서 평균 (노이즈만 약간 줄이기)
	for (uint8_t i = 0; i < N; i++)
	{
		acc += HX711_ReadRaw24();
		_delay_ms(2);
	}
	unsigned long sample = acc / N;

	// 2) 1차 저역 필터 (지수 이동 평균)만 적용
	static uint8_t first_run = 1;
	if (first_run)
	{
		g_hx711_raw = sample;  // 첫 번째는 그냥 박아 넣기
		first_run = 0;
	}
	else
	{
		g_hx711_raw = (g_hx711_raw * HX_FILTER_ALPHA_NUM
		+ sample * (HX_FILTER_ALPHA_DEN - HX_FILTER_ALPHA_NUM))
		/ HX_FILTER_ALPHA_DEN;
	}
}



// HX711 상태 플래그 업데이트
// 현재는 raw 값 보정 전이므로, 버튼 값을 이용해 g_speed_level을 갱신
// (나중에 버튼 로직 제거하고, g_hx711_raw를 기준으로 speed_level을 정하면 됨)
void HX_711_Update(void)
{
	unsigned long w = g_hx711_raw;
	uint8_t new_level = g_speed_level;

	switch (g_speed_level)
	{
		case 0:
		// 올라갈 때만 ON 임계값 사용
		if (w > LV3_ON)      new_level = 3;
		else if (w > LV2_ON) new_level = 2;
		else if (w > LV1_ON) new_level = 1;
		break;

		case 1:
		// 위로 갈 때
		if (w > LV3_ON)      new_level = 3;
		else if (w > LV2_ON) new_level = 2;
		// 아래로 갈 때 (히스테리시스)
		else if (w < LV1_OFF) new_level = 0;
		break;

		case 2:
		// 위로 갈 때
		if (w > LV3_ON) new_level = 3;
		// 아래로 갈 때
		else if (w < LV2_OFF)
		{
			if (w > LV1_ON)       new_level = 1;
			else if (w < LV1_OFF) new_level = 0;
		}
		break;

		case 3:
		default:
		// 아래로 내려갈 때만 OFF 임계값 사용
		if (w < LV3_OFF)
		{
			if (w > LV2_ON)       new_level = 2;
			else if (w > LV1_ON)  new_level = 1;
			else if (w < LV1_OFF) new_level = 0;
		}
		break;
	}

	g_speed_level = new_level;
}



// ===================== 버튼 관련 =====================
// 임시 상태 플래그 업데이트용 버튼 (PB2~PB0)

// PB0, PB1, PB2 사용
#define BTN_LV1   PB0   // 누르면 speed_level = 1
#define BTN_LV2   PB1   // 누르면 speed_level = 2
#define BTN_LV3   PB2   // 누르면 speed_level = 3

void Button_init(void)
{
    // 입력 + 내부 풀업
    DDRB  &= ~((1 << BTN_LV1) | (1 << BTN_LV2) | (1 << BTN_LV3));
    PORTB |=  ((1 << BTN_LV1) | (1 << BTN_LV2) | (1 << BTN_LV3));
}

// 버튼 상태를 읽어 g_speed_level 갱신
// 액티브-로우: 눌리면 0
void Button_Update(void)
{
	uint8_t pins = PINB;

	// 기본값: 이전 상태 유지
	uint8_t new_level = g_speed_level;

	// 우선순위: LV3 > LV2 > LV1
	if (!(pins & (1 << BTN_LV3))) {
		new_level = 3;
		} else if (!(pins & (1 << BTN_LV2))) {
		new_level = 2;
		} else if (!(pins & (1 << BTN_LV1))) {
		new_level = 1;
	}
	// else: 아무 버튼도 안 눌렸으면 new_level은 그대로 (유지)

	g_speed_level = new_level;
}



// ===================== 모터 / PWM 관련 =====================
// L298N 기준:
// PE4 : ENA (PWM, OC3B)
// PE5 : IN1
// PE6 : IN2

#define MOTOR_EN   PE4   // OC3B
#define MOTOR_IN1  PE5
#define MOTOR_IN2  PE6

void Motor_init(void)
{
	// 모터 제어 핀 출력 설정
	DDRE |= (1 << MOTOR_EN) | (1 << MOTOR_IN1) | (1 << MOTOR_IN2);

	// 초기 방향: IN1=HIGH, IN2=LOW → 정방향
	PORTE |=  (1 << MOTOR_IN1);
	PORTE &= ~(1 << MOTOR_IN2);

	// Timer3 모두 초기화
	TCCR3A = 0;
	TCCR3B = 0;
	TCCR3C = 0;

	// Timer3: Fast PWM 10bit, OC3B 비반전 모드
	// WGM3[3:0] = 0b0111 (10bit Fast PWM), COM3B1=1 (비반전, OC3B 사용)
	TCCR3A |= (1 << WGM30) | (1 << WGM31) | (1 << COM3B1);
	TCCR3B |= (1 << WGM32) | (1 << CS30) | (1 << CS31); // 분주비 64

	OCR3B = 0;   // 초기 duty 0
}


// HX_711에서 결정된 상태 플래그(g_speed_level)에 따라 duty 결정
// 요구사항:
//  state 3 -> duty 1023
//  state 2 -> duty  770
//  state 1 -> duty  530
//  state 0 -> duty    0
void Motor_Condition(void)
{
    switch (g_speed_level)
    {
        case 3:
            g_duty = 1023;
            break;
        case 2:
            g_duty = 770;
            break;
        case 1:
            g_duty = 530;
            break;
        case 0:
        default:
            g_duty = 0;
            break;
    }
}

// 실제 PWM 레지스터에 duty 반영
void Motor_Active(void)
{
    OCR3B = g_duty;
}


// ===================== LCD 관련 =====================
// LCD_init() : MCU_Init + LCDInit + 초기 화면 구성
// LCD_update(): 아랫줄의 raw 값 부분만 갱신 (D : raw의 raw만)

// Speed Level 숫자를 바꿀 때 사용할 이전 값 저장
static uint8_t prev_speed_level = 255;

void LCD_init(void)
{
    MCU_Init();   // 외부 메모리 및 포트 초기화 (LCD용)
    LCDInit();    // LCD 컨트롤러 초기화

    LCDCommand(ALLCLR);
    LCDCommand(HOME);

    // 1행: "Speed Level X"
    LCDMove(0, 0);
    LCDPuts("Speed Level ");
    LCDPutchar('0' + g_speed_level);  // 처음엔 0

    prev_speed_level = g_speed_level;

    // 2행: "D : "까지 고정 출력 (raw 숫자는 나중에 update에서만 갱신)
    LCDMove(1, 0);
    LCDPuts("D : ");
}

// speed_level이 바뀌었을 때만 1행의 마지막 숫자만 살짝 갱신
static void LCD_updateSpeedIfChanged(void)
{
    if (g_speed_level != prev_speed_level)
    {
        LCDMove(0, 12);               // "Speed Level "이 12글자 (0~11)
        LCDPutchar('0' + g_speed_level);
        prev_speed_level = g_speed_level;
    }
}

// LCD_update() : 2행의 raw 값 부분만 갱신
// 포맷: "D : 123456" 형태에서, 숫자 부분만 새로 쓰고 뒤에 공백으로 잔여 자리 클리어
void LCD_update(void)
{
	char buf[12];

	// 1. Speed Level 숫자는 바뀌었을 때만 살짝 갱신
	LCD_updateSpeedIfChanged();

	// 2행: raw + duty 찍기: "D:xxxxx U:yyyy"
	LCDMove(1, 0);
	LCDPuts("D:");
	ultoa(g_hx711_raw, buf, 10);
	LCDPuts(buf);

	LCDPuts(" U:");          // U = duty
	ultoa(g_duty, buf, 10);
	LCDPuts(buf);
	LCDPuts("   ");          // 남는 자리 클리어
}



// ===================== main =====================

int main(void)
{
    LCD_init();
    HX_711_Init();
    Button_init();
    Motor_init();

    while (1)
    {
        HX_711_Active();   // raw 읽기 (지금은 값만 저장)
        HX_711_Update();   // 나중에 raw 기반으로 g_speed_level을 갱신할 자리

        Button_Update();   // 현재는 버튼이 g_speed_level을 “대신” 갱신

        Motor_Condition(); // g_speed_level만 보고 duty 결정
        Motor_Active();

        LCD_update();      // Speed Level + raw 표시

        _delay_ms(100);
    }
}
