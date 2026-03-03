
CC = arm-linux-gnueabihf-gcc
# CC = /home/ssq/linux/IMX6ULL/tool/buildroot-2025.02.10/output/host/bin/arm-linux-gnueabihf-gcc

# 2. 编译选项 (CFLAGS)
# -Wall: 打印所有警告   -O2: 代码优化   -g: 包含调试信息(方便日后用gdb调试)
CFLAGS = -Wall -Wextra -std=gnu99 -O2 -g


LDFLAGS = -lpthread  -lm -ldl

# 4. 目标文件和源文件定义
TARGET = ui_app

SRCS = main.c \
       ui_get.c \
       socketUDP.c \
       sqlite_unit.c \
       mgmt_netlink.c \
       gpsget.c \
	sqlite3.c \
       Thread.c \
	Lock.c \
       mgmt_transmit.c 

# 自动把 .c 替换成 .o
OBJS = $(SRCS:.c=.o)

all: $(TARGET)

# 链接生成可执行文件
$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# 编译 C 文件生成 .o 目标文件
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# 清理工程：敲击 make clean 时执行
clean:
	rm -f $(OBJS) $(TARGET)

# 声明伪目标，防止和同名文件冲突
.PHONY: all clean