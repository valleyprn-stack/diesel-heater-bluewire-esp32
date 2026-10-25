#pragma once
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <cctype>
#include <cstdio>
#include <string>
#define F(x) (x)
typedef char __FlashStringHelper;
#define HEX 16
#define DEC 10
#define INPUT 0x0
#define OUTPUT 0x1
#define INPUT_PULLUP 0x2
#define LOW 0
#define HIGH 1
#define CHANGE 1
unsigned long micros();
unsigned long millis();
void delayMicroseconds(unsigned int us);
void noInterrupts();
void interrupts();
inline int digitalPinToInterrupt(int p) { return p == 2 ? 0 : 1; }
void attachInterrupt(int n, void (*f)(), int mode);
void detachInterrupt(int n);
void pinMode(int pin, int mode);
void digitalWrite(int pin, int val);
int digitalRead(int pin);
struct SerialMock {
    std::string out, in;
    void begin(long) {}
    int available() { return (int)in.size(); }
    int read() { if (in.empty()) return -1; int c = (unsigned char)in[0]; in.erase(0, 1); return c; }
    void print(const char *s) { out += s; emit(); }
    void print(char c) { out += c; emit(); }
    void print(int v, int b = DEC) { num((long)v, b); }
    void print(unsigned int v, int b = DEC) { num((long)v, b); }
    void print(long v, int b = DEC) { num(v, b); }
    void print(unsigned long v, int b = DEC) { num((long)v, b); }
    void print(unsigned char v, int b = DEC) { num((long)v, b); }
    void print(signed char v, int b = DEC) { num((long)v, b); }
    void println() { out += "\n"; emit(); }
    template <class T> void println(T v) { print(v); println(); }
    template <class T> void println(T v, int b) { print(v, b); println(); }
    void num(long v, int b) { char buf[32]; if (b == 16) snprintf(buf, 32, "%lX", (unsigned long)v); else snprintf(buf, 32, "%ld", v); out += buf; emit(); }
    void emit();
};
extern SerialMock Serial;
