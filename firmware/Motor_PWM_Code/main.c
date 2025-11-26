/*
 * PWM_test_code_1126.c
 *
 * Created: 2025-11-26 오전 9:58:24
 * Author : cchhy
 */ 

#define F_CPU 16000000UL
#include <avr/io.h>
#include <util/delay.h>


// PE4 PWM, PE5 IN1, PE6 IN2 으로 사용. TimerC 사용 + PB3:0 포트의 액티브-로우 버튼 사용
void GPIO_PWM_init()
{
	PORTB |= (1<<PB0)|(1<<PB1)|(1<<PB2)|(1<<PB3); // PB3:0 풀업
	DDRB &= ~((1<<PB0)|(1<<PB1)|(1<<PB2)|(1<<PB3)); // PB3:0 input
	
	PORTE &= ~((1<<PE4)|(1<<PE6)); // 4, 6번 포트 LOW 출력 기본
	PORTE |= (1<<PE5);
	DDRE |= (1<<PE4)|(1<<PE5)|(1<<PE6); // 4, 5, 6번 포트 output으로 설정
	
	TCCR3A |= (1<<WGM30)|(1<<WGM31)|(1<<COM3B1); // 10bit mode, OCR3B만 사용하고 OCR3A와 OCR3C(PE3, PE5)는 범용 GPIO로 사용
	TCCR3B |= (1<<CS30)|(1<<CS31)|(1<<WGM32); // 분주비 64 설정
}

// 상태 flag 정의
typedef struct {
	uint8_t stop;
	uint8_t slow;
	uint8_t middle;
	uint8_t fast;
	} speed;


speed Motor_state;


void condition()
{
	uint8_t pins = PINB;
	
	if (!(pins & (1<<PB0))) // PB0과 비교, stop 상태 flag 설정
	{
		Motor_state.stop = 1;
	} 
	else
	{
		Motor_state.stop = 0;
	}
	
	if (!(pins & (1<<PB1))) // PB1과 비교, slow 상태 flag 설정
	{
		Motor_state.slow = 1;
	}
	else
	{
		Motor_state.slow = 0;
	}
	
	if (!(pins & (1<<PB2))) // PB2와 비교, middle 상태 flag 설정
	{
		Motor_state.middle = 1;
	}
	else
	{
		Motor_state.middle = 0;
	}
	
	if (!(pins & (1<<PB3))) // PB3과 비교, fast 상태 flag 설정
	{
		Motor_state.fast = 1;
	}
	else
	{
		Motor_state.fast = 0;
	}
	return;
}

void Motor_Active(speed Motor_state)
{
	int duty = 0;
	
	if (Motor_state.stop)
	{
		duty = 0;
	} 
	else if (Motor_state.slow)
	{
		duty = 150;
	}
	else if (Motor_state.middle)
	{
		duty = 200;
	}
	else if (Motor_state.fast)
	{
		duty = 250;
	}
	
	OCR3B = duty;
}


int main(void)
{
    GPIO_PWM_init();
    while (1) 
    {
		condition();
		Motor_Active(Motor_state);	
		_delay_ms(20); // 소프트 디바운싱
    }
}

