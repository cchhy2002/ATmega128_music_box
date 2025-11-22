/*
 * HX_711_test.c
 *
 * Created: 2025-11-22 오후 4:41:50
 * Author : user
 */ 

#define F_CPU 16000000UL
#include <avr/io.h>
#include <util/delay.h>

void init() // 초기화, 기본적인 포트 설정이 필요할듯
{
	DDRE = 0xfe; // PE0 input(DOUT), PE1 output(SCK)
	PORTE = 0x00; 
}

int isReady() // HX711이 ready되었는지 확인, PIN으로 읽기만. 값을 return 할 필요가 있으므로 int로 상태 플래그 선언하기
{
	if ((PINE & 0x01) == 0)
	{
		return 0;
	} 
	else
	{
		return 1;
	}

}

unsigned long PulseClock(int READY) // READY 값을 가져와야 하므로 매개 변수로 설정하여, 위에서 return된 값을 지역 변수로 사용하기.
{
	unsigned long result = 0;
	
	if (READY == 0)
	{
		for (int i=0;i<24;i++) // A 채널 사용시, 무게 먼저 24번 읽기
		{
			PORTE |= (1<<PE1);
			_delay_ms(1);
			result = (result<<1)|(PINE & 0x01);
			PORTE &= ~(1<<PE1);
			_delay_ms(1);	
		}
		
		PORTE |= (1<<PE1); // A 채널, GAIN 128을 쓰므로 gain bit는 1개면 족함 1번만 유지.
		_delay_ms(1);
		PORTE &= ~(1<<PE1);
		_delay_ms(1);
	}
	return result;
}


void OutPut(unsigned long result) // 출력용 test 코드, 현재는 출력 기기가 없으므로 빈칸
{
}

int main(void) // return 값이 필요없음.
{
	init();
    while (1) 
    {
		int READY = isReady();
		unsigned long result = PulseClock(READY);
		OutPut(result);
    }
}

