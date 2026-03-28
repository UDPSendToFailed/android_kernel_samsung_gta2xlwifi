#ifndef __SEC_LOG_BUF_INDIRECT
#warning "samsung/debug/sec_log_buf.h is included directly."
#error "please include sec_debug.h instead of this file"
#endif

#ifndef __INDIRECT__SEC_LOG_BUF_H__
#define __INDIRECT__SEC_LOG_BUF_H__

#define SEC_LOG_MAGIC		0x4d474f4c

struct sec_log_buf {
	u32 boot_cnt;
	u32 magic;
	u32 idx;
	u32 prev_idx;
	char buf[];
};

#ifdef CONFIG_SEC_LOG_BUF_NO_CONSOLE
void sec_log_buf_write(const char *s, unsigned int count);
#else
static inline void sec_log_buf_write(const char *s, unsigned int count) {}
#endif

#endif /* __INDIRECT__SEC_LOG_BUF_H__ */