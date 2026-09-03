# ============================================================================
#  ชุดทดสอบไลบรารี Massmore_MAX3010x ที่รันบนเครื่อง PC
#
#  วิธีใช้
#      make          คอมไพล์แล้วรันทันที
#      make build    คอมไพล์อย่างเดียว
#      make clean    ลบไฟล์ที่สร้างขึ้น
# ============================================================================

CXX ?= g++
CXXFLAGS ?= -std=c++11 -Wall -Wextra -O1 -I. -I../lib/Massmore_MAX3010x/src

SOURCES = test_massmore_max3010x.cpp \
          massmore_max3010x_host_shim.cpp \
          ../lib/Massmore_MAX3010x/src/Massmore_MAX3010x.cpp \
          ../lib/Massmore_MAX3010x/src/Massmore_MAX3010x_Algorithms.cpp

TARGET = testrun

.PHONY: all build run clean

all: run

build: $(TARGET)

$(TARGET): $(SOURCES)
	$(CXX) $(CXXFLAGS) -o $@ $(SOURCES) -lm

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET) *.o
