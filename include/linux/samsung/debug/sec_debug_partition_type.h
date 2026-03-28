#ifndef __SEC_DEBUG_PARTITION_TYPE_H__
#define __SEC_DEBUG_PARTITION_TYPE_H__

#include "sec_debug_user_reset_type.h"

struct debug_partition_data_s {
	struct work_struct debug_partition_work;
	struct completion work;
	void *value;
	unsigned int offset;
	unsigned int size;
	unsigned int direction;
	long int error;
};

enum debug_partition_index {
	debug_index_reset_summary_info = 0,
	debug_index_reset_klog_info,
	debug_index_reset_tzlog_info,
	debug_index_reset_ex_info,
	debug_index_reset_summary,
	debug_index_reset_klog,
	debug_index_reset_tzlog,
	debug_index_ap_health,
	debug_index_reset_extrc_info,
	debug_index_lcd_debug_info,
	debug_index_modem_info,
	debug_index_auto_comment,
	debug_index_reset_history,
	debug_index_reset_rkplog,
	debug_index_max,
};

#define AP_HEALTH_MAGIC 0x48544C4145485041
#define AP_HEALTH_VER 2
#define MAX_PCIE_NUM 3

typedef struct {
	u64 magic;
	u32 size;
	u16 version;
	u16 need_write;
} ap_health_header_t;

struct edac_cnt {
	u32 ue_cnt;
	u32 ce_cnt;
};

typedef struct {
	struct edac_cnt edac[NR_CPUS][2];
	struct edac_cnt edac_l3;
	u32 edac_bus_cnt;
} cache_health_t;

typedef struct {
	u32 phy_init_fail_cnt;
	u32 link_down_cnt;
	u32 link_up_fail_cnt;
	u32 link_up_fail_ltssm;
} pcie_health_t;

typedef struct {
	u32 np;
	u32 rp;
	u32 mp;
	u32 kp;
	u32 dp;
	u32 wp;
	u32 tp;
	u32 sp;
	u32 pp;
	u32 cp;
} reset_reason_t;

enum {
	L3,
	PWR_CLUSTER,
	PERF_CLUSTER,
	PRIME_CLUSTER,
};

#define MAX_CLUSTER_NUM 4
#define CPU_NUM_PER_CLUSTER 4
#define MAX_VREG_CNT 3
#define MAX_BATT_DCVS 10

typedef struct {
	u32 cpu_KHz;
	u32 reserved;
} apps_dcvs_t;

typedef struct {
	u32 ddr_KHz;
	u16 mV[MAX_VREG_CNT];
} rpm_dcvs_t;

typedef struct {
	u64 ktime;
	s32 cap;
	s32 volt;
	s32 temp;
	s32 curr;
} batt_dcvs_t;

typedef struct {
	u32 tail;
	batt_dcvs_t batt[MAX_BATT_DCVS];
} battery_health_t;

typedef struct {
	u64 pon_reason;
	u64 fault_reason;
} pon_dcvs_t;

typedef struct {
	apps_dcvs_t apps[MAX_CLUSTER_NUM];
	rpm_dcvs_t rpm;
	batt_dcvs_t batt;
	pon_dcvs_t pon;
} dcvs_info_t;

typedef struct {
	ap_health_header_t header;
	u32 last_rst_reason;
	dcvs_info_t last_dcvs;
	u64 spare_magic1;
	reset_reason_t rr;
	cache_health_t cache;
	pcie_health_t pcie[MAX_PCIE_NUM];
	battery_health_t battery;
	u64 spare_magic2;
	reset_reason_t daily_rr;
	cache_health_t daily_cache;
	pcie_health_t daily_pcie[MAX_PCIE_NUM];
	u64 spare_magic3;
} ap_health_t;

#define MAX_FTOUT_NAME 128

struct lcd_debug_ftout {
	u32 count;
	char name[MAX_FTOUT_NAME];
};

struct lcd_debug_t {
	struct lcd_debug_ftout ftout;
};

#define DEBUG_PARTITION_MAGIC	0x41114729

#define PARTITION_RD				0
#define PARTITION_WR				1

#define DRV_UNINITIALIZED			0
#define DRV_INITIALIZED			1

#define SECTOR_UNIT_SIZE			4096

#define SEC_DEBUG_PARTITION_SIZE		(0xA00000)
#define SEC_DEBUG_RESET_HEADER_OFFSET		(0x0)
#define SEC_DEBUG_RESET_HEADER_SIZE		(SECTOR_UNIT_SIZE)
#define SEC_DEBUG_AP_HEALTH_OFFSET		(8 * 1024)
#define SEC_DEBUG_AP_HEALTH_SIZE		(sizeof(ap_health_t))
#define SEC_DEBUG_LCD_DEBUG_OFFSET		(12 * 1024)
#define SEC_DEBUG_LCD_DEBUG_SIZE		(4 * 1024)
#define SEC_DEBUG_EXTRA_INFO_OFFSET		(SEC_DEBUG_RESET_HEADER_OFFSET + SEC_DEBUG_RESET_HEADER_SIZE)
#define SEC_DEBUG_EXTRA_INFO_SIZE		(ALIGN(SEC_DEBUG_EX_INFO_SIZE, SECTOR_UNIT_SIZE))
#define SEC_DEBUG_RESET_MODEM_OFFSET		(1 * 1024 * 1024 - 8 * 1024)
#define SEC_DEBUG_RESET_MODEM_SIZE		(8 * 1024)
#define SEC_DEBUG_RESET_KLOG_OFFSET		(1 * 1024 * 1024)
#define SEC_DEBUG_RESET_KLOG_SIZE		(0x200000)
#define SEC_DEBUG_RESET_SUMMARY_OFFSET		(SEC_DEBUG_RESET_KLOG_OFFSET + ALIGN(SEC_DEBUG_RESET_KLOG_SIZE, SECTOR_UNIT_SIZE))
#define SEC_DEBUG_RESET_SUMMARY_SIZE		(0x200000)
#define SEC_DEBUG_RESET_TZLOG_OFFSET		(SEC_DEBUG_RESET_SUMMARY_OFFSET + ALIGN(SEC_DEBUG_RESET_SUMMARY_SIZE, SECTOR_UNIT_SIZE))
#define SEC_DEBUG_RESET_TZLOG_SIZE		(0x3000)
#define SEC_DEBUG_RESET_EXTRC_OFFSET		(SEC_DEBUG_RESET_TZLOG_OFFSET + ALIGN(SEC_DEBUG_RESET_TZLOG_SIZE, SECTOR_UNIT_SIZE))
#define SEC_DEBUG_RESET_EXTRC_SIZE		(2 * 1024)
#define SEC_DEBUG_AUTO_COMMENT_OFFSET		(SEC_DEBUG_RESET_EXTRC_OFFSET + ALIGN(SEC_DEBUG_RESET_EXTRC_SIZE, SECTOR_UNIT_SIZE))
#define SEC_DEBUG_AUTO_COMMENT_SIZE		(0x01000)
#define SEC_DEBUG_RESET_HISTORY_OFFSET		(16 * 1024)
#define SEC_DEBUG_RESET_HISTORY_MAX_CNT		(10)
#define SEC_DEBUG_RESET_HISTORY_SIZE		(SEC_DEBUG_AUTO_COMMENT_SIZE * SEC_DEBUG_RESET_HISTORY_MAX_CNT)
#define SEC_DEBUG_RESET_ETRM_SIZE		(0x3c0)
#define SEC_DEBUG_RESET_ETRM_OFFSET		(SEC_DEBUG_AUTO_COMMENT_OFFSET + ALIGN(SEC_DEBUG_AUTO_COMMENT_SIZE, SECTOR_UNIT_SIZE) - 0x15)

enum {
	DBG_PART_DRV_INIT_DONE,
	DBG_PART_DRV_INIT_EXIT,
};

#endif /* __SEC_DEBUG_PARTITION_TYPE_H__ */