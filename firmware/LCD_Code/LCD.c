/*
 * LCD.c
 * LCD 동작 함수 그룹
 * Created: 2019-09-24 오후 7:49:32
 *  Author: jong oh Park
 */ 
#define F_CPU 16000000UL
#include <avr/io.h>
#include <util/delay.h>
#include "lcd.h"

//#define LCD_PORT	PORTD
//#define LCD_DDR	DDRD
#define LCD_DATABUS	*((unsigned char *)0x2000) 	// LCD data output address
#define LCD_CONTROL	*((unsigned char *)0x2100) 	// LCD control signal output address

//내부함수

static void checkbusy(void);
//static void write_command(char command);
static void write_data(char ch);

//Initialing AVR Board
void MCU_Init(void)	/* initialize ATmege128 MCU */
{ 
     MCUCR = 0x80; 		// Enable external memory and I/O, Disable SLEEP mode
     XMCRA = 0x44; 		// 0x1100 - 0x7FFF (1 wait), 0x8000 - 0xFFFF (0 wait)
     XMCRB = 0x80; 		// Enable bus keeper. Use PC0-PC7 as high byte of address bus

     DDRB  = 0xF0;  		// PORTB = output
     PORTB = 0x00;

     DDRD  = 0x00;  		// PORTD = output
     PORTD = 0x00;

     DDRE  = 0x02;  		// PORTE = output 
     PORTE = 0x00;

     DDRF  = 0x00;  		// PORTF = Input
     PORTF = 0x00;             
     
     DDRG  = 0x1F;

     LCD_CONTROL = 0x00;
     LCD_DATABUS = 0x00;
}

//Initializing LCD
void LCDInit(void)
{
//****  4-bit MODE   *****
/*     LCD_PORT = 0x05;				// E=1,  RW=0, Rs=1
	 LCD_PORT = 0x01;				// E=0,  RW=0, Rs=1
*/	 

//****  8-bit MODE  *****
     LCD_CONTROL = 0x03;	// E = 1, Rs = 1 (dummy write)
     LCD_CONTROL = 0x02;	// E = 0, Rs = 1

     _delay_ms(2);

//	LCD_DDR = 0xFF;			//LCD PORT를 출력으로 설정
	_delay_ms(15);
	LCDCommand(0x30);
	_delay_ms(5);
	LCDCommand(0x30);
	_delay_ms(1);
	LCDCommand(0x32);
	
	LCDCommand(FUNSET);
	LCDCommand(DISP_OFF);
	LCDCommand(ALLCLR);
	LCDCommand(ENTMOD);
	LCDCommand(DISP_ON);
}

void LCDCommand(char command)
{
//****  4-bit MODE   *****
/*     LCD_PORT = 0x00;				// E=0,  RW=0, Rs=0
	 LCD_PORT = 0x04;				// E=1,  RW=0, Rs=0
*/	 

//****  8-bit MODE  *****
    	LCD_CONTROL = 0x00;		// E = 0, Rs = 0
     	LCD_CONTROL = 0x01;		// E = 1

		checkbusy();
     	LCD_DATABUS = command;	// output command
//     	asm (" PUSH  R0 ");		// delay for about 250 ns
//     	asm (" POP   R0 ");
     	LCD_CONTROL = 0x00;		// E = 0
 
//	write_command(command);
	if(command == ALLCLR || command == HOME)
		_delay_ms(2);
}

//LCD에 1문자 표시
void LCDPutchar(char ch)
{
	checkbusy();
	write_data(ch);
}

//LCD에 문자열 표시
void LCDPuts(char* str)
{
	while(*str !=0)			//*str이 Null문자인 경우 루프를 벗어남
	{						//while(*str)와 동일함
		LCDPutchar(*str);
		str++;				//str이 다음 표시할 문자를 가리킴
	}		
}

//표시할 문자의 위치를 선택
void LCDMove(char line, char pos)
{
	pos = (line << 6) + pos; 
  	pos |= 0x80;			// 비트 7를 세트한다.

  	LCDCommand(pos);
}

//LCD에 명령을 써넣음
/*
static void write_command(char command)
{ 
	char	temp;

	// 상위 nibble 출력	
	temp = (command & 0xF0) | 0x04;	//0x04: RS, RW=0, E=1
	LCD_PORT = temp;
	LCD_PORT = temp & ~0x04;		//E=0, LCD Disable

	// 하위 nibble 출력
	temp = (command << 4) | 0x04;		//0x04: RS, RW=0, E=1
	LCD_PORT = temp;
	LCD_PORT = temp & ~0x04;		//E=0, LCD Disable
	_delay_us(1) ;	
} */

//LCD에 Data를 써넣음
static void write_data(char ch)
{
    	LCD_CONTROL = 0x02;		// E = 0, Rs = 1
     	LCD_CONTROL = 0x03;		// E = 1
		checkbusy();
     	LCD_DATABUS = ch;		// output data
//     	asm (" PUSH  R0 ");		// delay for about 250 ns
//     	asm (" POP   R0 ");
     	LCD_CONTROL = 0x02;		// E = 0
     	_delay_us(50);

/*    4bit Mode
	unsigned char	temp;
     
     // 상위 nibble 출력	
	temp = (ch & 0xF0) | 0x05;	//0x05: RS=1, RW=0, E=1
	LCD_PORT = temp;
	LCD_PORT = temp & ~0x04;		//E=0, LCD Disable

    // 하위 nibble 출력
	temp = (ch << 4) | 0x05;	//0x05: RS=1, RW=0, E=1
	LCD_PORT = temp;
	LCD_PORT = temp & ~0x04;		//E=0, LCD Disable
*/
}

//
void LCDNewchar(char ch, char font[])	// 글자 등록함수
{
	int i;
		
	ch <<= 3;		 // ch = ch << 3;과 같음
	ch |= 0x40;		// 비트6을 세트 => CGRAM 주소설정

	LCDCommand(ch);	// CGRAM 주소설정 =>LCDPutchar()로 
					// 쓰는 문자는 CGRAM에 저장

	for(i=0; i<8; i++)	// 글꼴을 CGRAM에 저장한다.
		LCDPutchar(font[i]);
}

//Busy FLAG 체크, 대략 50~100us 시간지연으로 대체
static void checkbusy()
{
  	_delay_us(100);		//_delay_us(100)
  	return;
}