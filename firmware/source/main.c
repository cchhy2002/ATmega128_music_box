/*
 * HX711 + PWM + LCD + WS2812B + Pump 통합 예제 (리팩토링+확장 버전)
 *
 * - HX711 raw 값을 읽어서 LCD 2줄로 표시
 *   1줄: "Speed Level X"
 *   2줄: "D:xxxxx U:yyyy"
 *
 * - HX711은 ATmega128의 PORTD 사용
 *   DOUT : PD0
 *   SCK  : PD1
 *
 * - 현재는 HX711 raw 값으로 speed_level(0~3)을 직접 결정
 *   (버튼은 디버깅용 + 펌프 제어용)
 *
 * - DC 모터: Timer3 OC3B(PE4) 10bit Fast PWM
 * - WS2812B: PE2, 비트뱅잉 (GRB)
 * - 솔레노이드 펌프: PE6, PB0(active-low) 버튼을 누르는 동안 동작
 */

#ifndef F_CPU
#define F_CPU 16000000UL
#endif

#include <avr/io.h>
#include <util/delay.h>
#include <stdlib.h>
#include <stdbool.h>
#include <avr/interrupt.h>
#include "lcd.h"   // MCU_Init(), LCDInit(), LCDCommand(), LCDMove(), LCDPuts() 등

// ===================== 공통 전역 변수 =====================

// HX711에서 읽은 24비트 raw 값 (EMA 필터 후 값)
volatile unsigned long g_hx711_raw = 0;

// 모터/속도 제어용 상태 플래그 (0~3)
volatile uint8_t  g_speed_level = 0;   // 0: stop, 1/2/3: 점점 빠르게

// 모터 PWM duty (0~1023, Fast PWM 10bit)
volatile uint16_t g_duty = 0;

// 전역 millis 카운터 (1ms 단위)
volatile uint32_t g_millis = 0;

// ===================== 타이머0 기반 1ms tick / millis() =====================

// Timer0 Compare Match ISR: 1ms마다 호출되도록 설정
ISR(TIMER0_COMP_vect)
{
    g_millis++;
}

// Timer0: CTC 모드, 1ms 주기 인터럽트
void Timer0_init(void)
{
    // F_CPU = 16MHz, prescaler = 64
    // 16MHz / 64 = 250kHz → 1ms = 250카운트 → 0~249
    TCCR0 = (1 << WGM01) | (1 << CS01) | (1 << CS00); // CTC 모드, 분주 64
    OCR0  = 249;                                      // 1ms마다 비교 일치
    TIMSK |= (1 << OCIE0);                            // Timer0 Compare Match 인터럽트 enable
}

// 현재까지 누적된 ms 반환 (인터럽트 구간 보호)
uint32_t millis(void)
{
    uint32_t m;
    cli();
    m = g_millis;
    sei();
    return m;
}

// ===================== HX711 필터 / 레벨 임계값 =====================

// 1차 저역 필터(지수 이동 평균) 계수: out = (9/10)*out + (1/10)*in
#define HX_FILTER_ALPHA_NUM   9
#define HX_FILTER_ALPHA_DEN   10

// 각 레벨 사이의 중간값을 임계값으로 사용
#define TH_01 348000UL   // 0↔1 경계
#define TH_12 295000UL   // 1↔2 경계
#define TH_23 242000UL   // 2↔3 경계

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

// HX711 핀 방향 및 초기 상태 설정
void HX_711_Init(void)
{
    // DOUT 입력, SCK 출력
    DDRD &= ~(1 << HX_DOUT_PD);   // 입력
    DDRD |=  (1 << HX_SCK_PD);    // 출력

    // DOUT 풀업 OFF, SCK idle LOW
    PORTD &= ~(1 << HX_DOUT_PD);
    PORTD &= ~(1 << HX_SCK_PD);

    // 초기값 한 번 읽어서 g_hx711_raw에 넣어둠
    g_hx711_raw = HX711_ReadRaw24();
}

// HX711 구동: 여러 번 읽어 평균 내고 EMA 필터 적용
void HX_711_Active(void)
{
    const uint8_t N = 5;
    unsigned long acc = 0;

    // 1) N회 샘플링해서 평균 (노이즈 감소)
    for (uint8_t i = 0; i < N; i++)
    {
        acc += HX711_ReadRaw24();
        _delay_ms(2);
    }
    unsigned long sample = acc / N;

    // 2) 1차 저역 필터 (지수 이동 평균)
    static uint8_t first_run = 1;
    if (first_run)
    {
        g_hx711_raw = sample;  // 첫 번째는 그대로 사용
        first_run = 0;
    }
    else
    {
        g_hx711_raw =
            (g_hx711_raw * HX_FILTER_ALPHA_NUM
           + sample       * (HX_FILTER_ALPHA_DEN - HX_FILTER_ALPHA_NUM))
           / HX_FILTER_ALPHA_DEN;
    }
}

// HX711 상태 플래그 업데이트
// g_hx711_raw의 절대값에 따라 speed_level(0~3) 결정
void HX_711_Update(void)
{
    unsigned long d = g_hx711_raw;
    uint8_t level;

    if (d >= TH_01)
    {
        // 거의 빈 상태 (444xxx 근처) → Level 0
        level = 0;
    }
    else if (d >= TH_12)
    {
        // 295000 ~ 382000 → Level 1 (대략 32xxxx 근처)
        level = 1;
    }
    else if (d >= TH_23)
    {
        // 242000 ~ 295000 → Level 2 (대략 27xxxx 근처)
        level = 2;
    }
    else
    {
        // 242000 미만 → Level 3 (대략 215xxx 근처)
        level = 3;
    }

    g_speed_level = level;
}

// ===================== 버튼 관련 (디버깅 + 펌프용) =====================

// PB0, PB1, PB2 사용
#define BTN_LV1   PB0   // 누르면 Level 1 / 펌프 트리거
#define BTN_LV2   PB1   // 디버깅용 Level 2
#define BTN_LV3   PB2   // 디버깅용 Level 3

void Button_init(void)
{
    // 입력 + 내부 풀업
    DDRB  &= ~((1 << BTN_LV1) | (1 << BTN_LV2) | (1 << BTN_LV3));
    PORTB |=  ((1 << BTN_LV1) | (1 << BTN_LV2) | (1 << BTN_LV3));
}

// 버튼 상태를 읽어 g_speed_level 갱신 (디버깅용)
// 액티브-로우: 눌리면 0
void Button_Update(void)
{
    uint8_t pins = PINB;

    // 기본값: 이전 상태 유지
    uint8_t new_level = g_speed_level;

    // 우선순위: LV3 > LV2 > LV1
    if (!(pins & (1 << BTN_LV3)))      new_level = 3;
    else if (!(pins & (1 << BTN_LV2))) new_level = 2;
    else if (!(pins & (1 << BTN_LV1))) new_level = 1;
    // else: 아무 버튼도 안 눌렸으면 new_level 유지

    g_speed_level = new_level;
}

// ===================== 모터 / PWM 관련 =====================
// DC 모터는 이제 ENA(PE4: OC3B)만 사용한다고 가정 (L298N IN1/IN2 미사용)

#define MOTOR_EN   PE4   // OC3B

void Motor_init(void)
{
    // 모터 PWM 핀 출력 설정
    DDRE |= (1 << MOTOR_EN);

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
void Motor_Condition(void)
{
    switch (g_speed_level)
    {
        case 3:  g_duty = 300; break;
        case 2:  g_duty = 230; break;
        case 1:  g_duty = 175; break;
        case 0:
        default: g_duty =   0; break;
    }
}

// 실제 PWM 레지스터에 duty 반영
void Motor_Active(void)
{
    OCR3B = g_duty;
}

// ===================== WS2812B / LED 관련 =====================
// - PE2 사용 (GRB 순서)
// - LED_init(), LED_Condition(), LED_Update()

#define WS2812_PORT  PORTE
#define WS2812_DDR   DDRE
#define WS2812_BIT   2        // PE2
#define LED_PIN      PE2
#define NUM_LEDS     4

typedef struct {
    uint8_t g;
    uint8_t r;
    uint8_t b;
} NeoPixel;

volatile NeoPixel g_led_buf[NUM_LEDS];

// 깜빡임용 상태: ON일 때 색, 깜빡임 주기(ms)
volatile uint16_t g_led_interval_ms = 0;  // 몇 ms마다 ON/OFF 토글할지
volatile uint8_t  g_led_color_r     = 0;
volatile uint8_t  g_led_color_g     = 0;
volatile uint8_t  g_led_color_b     = 0;

// 바이트 하나(8비트)를 WS2812 규격으로 보내는 함수 (어셈블리 기반)
static inline void ws2812_sendByte(uint8_t byte)
{
    for (uint8_t i = 0; i < 8; i++)
    {
        if (byte & 0x80)
        {
            // ===== '1' 비트 패턴 =====
            asm volatile (
                // HIGH 구간 (길게)
                "sbi %[port], %[bit] \n\t"          // 2 clk
                "nop \n\t""nop \n\t""nop \n\t"      // 3
                "nop \n\t""nop \n\t""nop \n\t"      // 3 -> 총 6 NOP = 6 clk

                // LOW 구간 (짧게)
                "cbi %[port], %[bit] \n\t"          // 2 clk
                "nop \n\t""nop \n\t""nop \n\t"      // 3 clk
                :
                : [port] "I" (_SFR_IO_ADDR(WS2812_PORT)),
                  [bit]  "I" (WS2812_BIT)
            );
        }
        else
        {
            // ===== '0' 비트 패턴 =====
            asm volatile (
                // HIGH 구간 (짧게)
                "sbi %[port], %[bit] \n\t"          // 2 clk
                "nop \n\t""nop \n\t"                // 2 clk

                // LOW 구간 (길게)
                "cbi %[port], %[bit] \n\t"          // 2 clk
                "nop \n\t""nop \n\t""nop \n\t"      // 3
                "nop \n\t""nop \n\t""nop \n\t"      // 3 -> 6 clk
                :
                : [port] "I" (_SFR_IO_ADDR(WS2812_PORT)),
                  [bit]  "I" (WS2812_BIT)
            );
        }

        byte <<= 1;   // 다음 비트로 이동
    }
}

static void ws2812_show(void)
{
    // 타이밍 정확도를 위해 인터럽트 잠시 비활성
    cli();
    for (uint8_t i = 0; i < NUM_LEDS; i++)
    {
        // GRB 순서
        ws2812_sendByte(g_led_buf[i].g);
        ws2812_sendByte(g_led_buf[i].r);
        ws2812_sendByte(g_led_buf[i].b);
    }
    sei();

    // 리셋 (LOW > 50us)
    _delay_us(60);
}

void LED_init(void)
{
    WS2812_DDR |= (1 << LED_PIN);        // 출력
    WS2812_PORT &= ~(1 << LED_PIN);      // LOW

    for (uint8_t i = 0; i < NUM_LEDS; i++)
    {
        g_led_buf[i].g = 0;
        g_led_buf[i].r = 0;
        g_led_buf[i].b = 0;
    }

    ws2812_show(); // 초기 상태(OFF) 전송
}

// Condition: g_speed_level에 따라 색/깜빡임 주기(ms) 설정
// 0: OFF
// 1: WHITE (10,10,10), 300ms마다 ON/OFF 토글
// 2: WHITE (10,10,10), 200ms마다 ON/OFF 토글
// 3: WHITE (10,10,10), 100ms마다 ON/OFF 토글
void LED_Condition(void)
{
    uint8_t  r = 0, g = 0, b = 0;
    uint16_t interval = 0;   // ms 단위

    switch (g_speed_level)
    {
        case 1:
            r = g = b = 20;
            interval = 200;   // 300ms마다 ON/OFF 토글
            break;
        case 2:
            r = g = b = 20;
            interval = 100;   // 200ms마다 ON/OFF 토글
            break;
        case 3:
            r = g = b = 20;
            interval = 50;   // 100ms마다 ON/OFF 토글
            break;
        case 0:
        default:
            r = g = b = 0;
            interval = 0;     // 깜빡임 없음 (항상 OFF)
            break;
    }

    g_led_color_r     = r;
    g_led_color_g     = g;
    g_led_color_b     = b;
    g_led_interval_ms = interval;
}

// LED_Update() : millis() 기반으로 블로킹 없이 깜빡임 제어 + WS2812B 전송
void LED_Update(void)
{
    static uint8_t  led_on         = 0;  // 현재 ON/OFF 상태
    static uint32_t last_toggle_ms = 0;  // 마지막 토글 시각(ms)

    uint16_t interval = g_led_interval_ms;
    uint32_t now      = millis();

    if (interval == 0)
    {
        // speed_level 0: 항상 꺼짐
        led_on         = 0;
        last_toggle_ms = now;

        for (uint8_t i = 0; i < NUM_LEDS; i++)
        {
            g_led_buf[i].g = 0;
            g_led_buf[i].r = 0;
            g_led_buf[i].b = 0;
        }
        ws2812_show();
        return;
    }

    // interval(ms) 이상 경과하면 ON/OFF 토글
    if ((uint32_t)(now - last_toggle_ms) >= interval)
    {
        last_toggle_ms = now;
        led_on = !led_on;

        uint8_t r = led_on ? g_led_color_r : 0;
        uint8_t g = led_on ? g_led_color_g : 0;
        uint8_t b = led_on ? g_led_color_b : 0;

        for (uint8_t i = 0; i < NUM_LEDS; i++)
        {
            g_led_buf[i].g = g;
            g_led_buf[i].r = r;
            g_led_buf[i].b = b;
        }
        ws2812_show();
    }
    // 아직 interval이 안 지났으면 아무 것도 안 함 (비블로킹)
}

// ===================== 솔레노이드 펌프 관련 =====================
// - PE6 사용
// - PB0(active-low)을 누르는 동안 ON
// - 완전 비블로킹

#define PUMP_PIN   PE6

void Pump_init(void)
{
    DDRE |= (1 << PUMP_PIN);      // 출력
    PORTE &= ~(1 << PUMP_PIN);    // OFF
}

// PB0 버튼 상태에 따라 펌프 ON/OFF
void Pump_Update(void)
{
    static bool pump_on = false;

    // 액티브 로우: 눌리면 0
    if (!(PINB & (1 << BTN_LV1)))
        pump_on = true;
    else
        pump_on = false;

    if (pump_on)
        PORTE |= (1 << PUMP_PIN);
    else
        PORTE &= ~(1 << PUMP_PIN);
}

// ===================== LCD 관련 =====================

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

    // 2행은 LCD_update()에서 전체 갱신
}

// speed_level이 바뀌었을 때만 1행의 마지막 숫자 갱신
static void LCD_updateSpeedIfChanged(void)
{
    if (g_speed_level != prev_speed_level)
    {
        LCDMove(0, 12);               // "Speed Level "이 12글자 (0~11)
        LCDPutchar('0' + g_speed_level);
        prev_speed_level = g_speed_level;
    }
}

// LCD_update() : 2행에 raw + duty 표시
// 포맷: "D:xxxxx U:yyyy"
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
    Button_init();    // 디버깅 + 펌프 입력용
    Motor_init();
    LED_init();
    Pump_init();
    Timer0_init();    // 1ms tick 타이머
    sei();            // 전역 인터럽트 enable (Timer0 + 기타)

    while (1)
    {
        HX_711_Active();   // HX711 필터링된 raw 읽기
        HX_711_Update();   // raw 기반으로 g_speed_level 결정

        // ↓ 디버깅용: 버튼으로 speed_level 강제 제어하고 싶으면 주석 해제
        //Button_Update();

        Motor_Condition(); // g_speed_level만 보고 duty 결정
        Motor_Active();    // PWM 반영

        LCD_update();      // Speed Level + raw + duty 표시

        LED_Condition();   // g_speed_level 기준 색/주기 설정
        LED_Update();      // millis() 기반으로 네오픽셀 깜빡임 제어

        Pump_Update();     // PB0 상태 기반 솔레노이드 펌프 제어 (비블로킹)

        _delay_ms(40);     // 루프 주기 완화용 (타이밍은 Timer0가 관리)
    }
}
