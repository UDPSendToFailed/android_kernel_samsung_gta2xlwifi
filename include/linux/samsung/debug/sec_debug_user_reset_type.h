#ifndef __SEC_DEBUG_USER_RESET_TYPE_H__
#define __SEC_DEBUG_USER_RESET_TYPE_H__

enum extra_info_dbg_type {
	DBG_0_RESERBED = 0,
	DBG_1_UFS_ERR,
	DBG_2_RESERVED,
	DBG_3_RESERVED,
	DBG_4_RESERVED,
	DBG_5_RESERVED,
	DBG_MAX,
};

#define EXINFO_RPM_LOG_SIZE			50
#define EXINFO_TZ_LOG_SIZE			40
#define EXINFO_HYP_LOG_SIZE			460

typedef struct {
	unsigned int esr;
	char str[52];
	u64 var1;
	u64 var2;
	u64 pte[6];
} ex_info_fault_t;

extern ex_info_fault_t ex_info_fault[NR_CPUS];

typedef struct {
	char dev_name[24];
	u32 fsr;
	u32 fsynr0;
	u32 fsynr1;
	unsigned long iova;
	unsigned long far;
	char mas_name[24];
	u8 cbndx;
	phys_addr_t phys_soft;
	phys_addr_t phys_atos;
	u32 sid;
} ex_info_smmu_t;

typedef struct {
	int reason;
	char handler_str[24];
	unsigned int esr;
	char esr_str[32];
} ex_info_badmode_t;

typedef struct {
	u64 ktime;
	u32 extc_idx;
	u32 upload_cause;
	int cpu;
	char task_name[TASK_COMM_LEN];
	char bug_buf[64];
	char panic_buf[64];
	ex_info_smmu_t smmu;
	ex_info_fault_t fault[NR_CPUS];
	ex_info_badmode_t badmode;
	char pc[64];
	char lr[64];
	char ufs_err[23];
	u32 lpm_state[NR_CPUS];
	u64 lr_val[NR_CPUS];
	u64 pc_val[NR_CPUS];
	u32 pko;
	char backtrace[0];
} _kern_ex_info_t;

typedef struct {
	u64 nsec;
	u32 arg[4];
	char msg[EXINFO_RPM_LOG_SIZE];
} __rpm_log_t;

typedef struct {
	u32 magic;
	u32 ver;
	u32 nlog;
	__rpm_log_t log[5];
} _rpm_ex_info_t;

typedef struct {
	_rpm_ex_info_t info;
} rpm_exinfo_t;

typedef struct {
	u8 cpu_status[NR_CPUS];
	char msg[EXINFO_TZ_LOG_SIZE];
} tz_exinfo_t;

typedef struct {
	u32 esr;
	u32 ear0;
	u32 esr_sdi;
	u32 ear0_sdi;
} pimem_exinfo_t;

typedef struct {
	s64 cpu[NR_CPUS];
} cpu_stuck_exinfo_t;

typedef struct {
	int s2_fault_counter;
	char msg[EXINFO_HYP_LOG_SIZE];
} hyp_exinfo_t;

#define EXINFO_RPM_DATA_SIZE			sizeof(rpm_exinfo_t)
#define EXINFO_TZ_DATA_SIZE			sizeof(tz_exinfo_t)
#define EXINFO_PIMEM_DATA_SIZE			sizeof(pimem_exinfo_t)
#define EXINFO_CPU_STUCK_DATA_SIZE		sizeof(cpu_stuck_exinfo_t)
#define EXINFO_HYP_DATA_SIZE			sizeof(hyp_exinfo_t)
#define EXINFO_SUBSYS_DATA_SIZE			(EXINFO_RPM_DATA_SIZE + EXINFO_TZ_DATA_SIZE + EXINFO_PIMEM_DATA_SIZE + EXINFO_CPU_STUCK_DATA_SIZE + EXINFO_HYP_DATA_SIZE)
#define EXINFO_KERNEL_SPARE_SIZE		(2048 - EXINFO_SUBSYS_DATA_SIZE)
#define EXINFO_KERNEL_DEFAULT_SIZE		2048

typedef union {
	_kern_ex_info_t info;
	char ksize[EXINFO_KERNEL_DEFAULT_SIZE + EXINFO_KERNEL_SPARE_SIZE];
} kern_exinfo_t;

#define RPM_EX_INFO_MAGIC 0x584D5052

typedef struct {
	kern_exinfo_t kern_ex_info;
	rpm_exinfo_t rpm_ex_info;
	tz_exinfo_t tz_ex_info;
	pimem_exinfo_t pimem_info;
	cpu_stuck_exinfo_t cpu_stuck_info;
	hyp_exinfo_t hyp_ex_info;
} rst_exinfo_t;

#define SEC_DEBUG_EX_INFO_SIZE	(sizeof(rst_exinfo_t))

enum debug_reset_header_state {
	DRH_STATE_INIT = 0,
	DRH_STATE_VALID,
	DRH_STATE_INVALID,
	DRH_STATE_MAX,
};

struct debug_reset_header {
	u32 magic;
	u32 write_times;
	u32 read_times;
	u32 ap_klog_idx;
	u32 summary_size;
	u32 stored_tzlog;
	u32 fac_write_times;
	u32 auto_comment_size;
	u32 reset_history_valid;
	u32 reset_history_cnt;
};

#define DEBUG_RESET_HEAER_SIZE			(sizeof(struct debug_reset_header))

#define TZ_DIAG_LOG_MAGIC 0x747a6461

#define TZBSP_MAX_CPU_COUNT			0x08
#define TZBSP_DIAG_NUM_OF_VMID			16
#define TZBSP_DIAG_VMID_DESC_LEN		7
#define TZBSP_DIAG_INT_NUM			64
#define TZBSP_MAX_INT_DESC			16

#define TZBSP_AES_256_ENCRYPTED_KEY_SIZE	256
#define TZBSP_NONCE_LEN			12
#define TZBSP_TAG_LEN			16

enum tz_boot_info_cpu_status_type {
	TZ_BOOT_INFO_NONE = 0,
	RUNNING,
	POWER_COLLAPSED,
	WARM_BOOTING,
	INVALID_WARM_ENTRY_EXIT_COUNT,
	INVALID_WARM_TERM_ENTRY_EXIT_COUNT,
};

struct tzdbg_vmid_t {
	u8 vmid;
	u8 desc[TZBSP_DIAG_VMID_DESC_LEN];
};

struct tzdbg_boot_info_t {
	u32 wb_entry_cnt;
	u32 wb_exit_cnt;
	u32 pc_entry_cnt;
	u32 pc_exit_cnt;
	u32 warm_jmp_addr;
	u32 spare;
};

struct tzdbg_boot_info64_t {
	u32 wb_entry_cnt;
	u32 wb_exit_cnt;
	u32 pc_entry_cnt;
	u32 pc_exit_cnt;
	u32 psci_entry_cnt;
	u32 psci_exit_cnt;
	u64 warm_jmp_addr;
	u32 warm_jmp_instr;
};

struct tzdbg_reset_info_t {
	u32 reset_type;
	u32 reset_cnt;
};

struct tzdbg_int_t {
	u16 int_info;
	u8 avail;
	u8 spare;
	u32 int_num;
	u8 int_desc[TZBSP_MAX_INT_DESC];
	u32 int_count[TZBSP_MAX_CPU_COUNT];
};

struct tzdbg_int_t_tz40 {
	u16 int_info;
	u8 avail;
	u8 spare;
	u32 int_num;
	u8 int_desc[TZBSP_MAX_INT_DESC];
	u32 int_count[TZBSP_MAX_CPU_COUNT];
};

struct tzbsp_diag_wakeup_info_t {
	u32 HPPIR;
	u32 AHPPIR;
};

struct tzdbg_log_pos_t {
	u16 wrap;
	u16 offset;
};

struct tzdbg_log_t {
	struct tzdbg_log_pos_t log_pos;
	u8 log_buf[];
};

struct tzdbg_t {
	u32 magic_num;
	u32 version;
	u32 cpu_count;
	u32 vmid_info_off;
	u32 boot_info_off;
	u32 reset_info_off;
	u32 int_info_off;
	u32 ring_off;
	u32 ring_len;
	u32 wakeup_info_off;
	struct tzdbg_vmid_t vmid_info[TZBSP_DIAG_NUM_OF_VMID];
	struct tzdbg_boot_info64_t boot_info[TZBSP_MAX_CPU_COUNT];
	struct tzdbg_reset_info_t reset_info[TZBSP_MAX_CPU_COUNT];
	u32 num_interrupts;
	struct tzdbg_int_t int_info[TZBSP_DIAG_INT_NUM];
	struct tzbsp_diag_wakeup_info_t wakeup_info[TZBSP_MAX_CPU_COUNT];
	u8 key[TZBSP_AES_256_ENCRYPTED_KEY_SIZE];
	u8 nonce[TZBSP_NONCE_LEN];
	u8 tag[TZBSP_TAG_LEN];
	struct tzdbg_log_t ring_buffer;
};

#endif /* __SEC_DEBUG_USER_RESET_TYPE_H__ */