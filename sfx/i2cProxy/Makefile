# 目标文件名
TARGET = demo_i2c

# 源文件
SRCS = demo_main.c i2c_proxy_aardvark.c aardvark.c

# 编译器
CC = gcc

# 编译选项
CFLAGS = -Wall -g


# 链接选项
LDFLAGS =  -lpthread

# 生成目标
$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# 清理
clean:
	rm -f $(TARGET)

