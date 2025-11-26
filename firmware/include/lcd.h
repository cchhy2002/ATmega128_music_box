/*
 * lcd.h
 * HD44780 Compatible 
 * Created: 2019-09-24 오후 7:50:48
 *  Author: Jong Oh Park
 */ 


#ifndef _LCD_H_
#define _LCD_H_

//LCD control command
#define ALLCLR				0x01	//화면을 지움
#define HOME				0x02	//커서를 홈으로 이동
#define LN21				0xc0	//커서를 2번째출 첫째 칸으로 이동
#define ENTMOD				0x06	//Entry mode
#define FUNSET				0x38	//Function set, 8-bit mode
#define DISP_ON				0x0c	//turn on Display.
#define DISP_OFF			0x08	//turn off Display.
#define CURSOR_ON			0x0e	//turn on Cursor.
#define CURSOR_OFF			0x0c	//turn off Cursor.
#define CURSOR_LSHIFT		0x10	//커서를 좌측으로 이동
#define CURSOR_RSHIFT		0x14	//커서를 우측으로 이동
#define DISP_LSHIFT			0x18	//Display를 좌측으로 이동
#define DISP_RSHIFT			0x1c	//Display를 우측으로 이동

//LCD control Function
void MCU_Init(void);
void LCDInit(void);
void LCDCommand(char	command);
void LCDMove(char line, char pos);
void LCDPutchar(char ch);
void LCDPuts(char* str);
void LCDNewchar(char ch, char font[]);

#endif /* _LCD_H_ */