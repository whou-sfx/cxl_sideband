# 编译器
CC = gcc

# 编译选项
CFLAGS = -Wall -g

# 链接选项
LDFLAGS = -lpthread

# 通用源文件
COMMON_SRCS = demo_main.c i2c_proxy_aardvark.c

# demo_i2c 使用 aardvark.c
demo_i2c_SRCS = $(COMMON_SRCS) aardvark.c

# demo_i2c_fake 使用 aardvark_fake.c
demo_i2c_fake_SRCS = $(COMMON_SRCS) aardvark_fake.c

# 默认目标
all: demo_i2c demo_i2c_fake

# 生成 demo_i2c
demo_i2c: $(demo_i2c_SRCS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# 生成 demo_i2c_fake
demo_i2c_fake: $(demo_i2c_fake_SRCS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# 清理
clean:
	rm -f demo_i2c demo_i2c_fake

