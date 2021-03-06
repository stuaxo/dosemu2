/* this is for the DPMI support */
#ifndef DPMI_H
#define DPMI_H

#include "emu-ldt.h"
#include "emm.h"
#include "dmemory.h"

#define DPMI_VERSION   		0x00	/* major version 0 */
#define DPMI_DRIVER_VERSION	0x5a	/* minor version 0.90 */

#define DPMI_MAX_CLIENTS	32	/* maximal number of clients */

#define DPMI_page_size		4096	/* 4096 bytes per page */

#define DPMI_pm_stack_size	0xf000	/* locked protected mode stack for exceptions, */
					/* hardware interrupts, software interrups 0x1c, */
					/* 0x23, 0x24 and real mode callbacks */

#define DPMI_rm_stacks		6	/* RM stacks per DPMI client */
#define DPMI_max_rec_rm_func	(DPMI_MAX_CLIENTS * DPMI_rm_stacks)	/* max number of recursive real mode functions */
#define DPMI_rm_stack_size	0x0200	/* real mode stack size */

#define DPMI_private_paragraphs	((DPMI_rm_stacks * DPMI_rm_stack_size)>>4)
					/* private data for DPMI server */
#define current_client ({ assert(in_dpmi); in_dpmi-1; })
#define DPMI_CLIENT (DPMIclient[current_client])
#define PREV_DPMI_CLIENT (DPMIclient[current_client-1])

#ifdef __linux__
int modify_ldt(int func, void *ptr, unsigned long bytecount);
#define LDT_WRITE 0x11
#endif
void direct_ldt_write(int offset, int length, char *buffer);

/* this is used like: SEL_ADR(_ss, _esp) */
void *SEL_ADR(unsigned short sel, unsigned int reg);
void *SEL_ADR_CLNT(unsigned short sel, unsigned int reg, int is_32);

#define HLT_OFF(addr) ((unsigned long)addr-(unsigned long)DPMI_dummy_start)

enum { es_INDEX, cs_INDEX, ss_INDEX, ds_INDEX, fs_INDEX, gs_INDEX,
  eax_INDEX, ebx_INDEX, ecx_INDEX, edx_INDEX, esi_INDEX, edi_INDEX,
  ebp_INDEX, esp_INDEX, eip_INDEX, eflags_INDEX };

typedef struct pmaddr_s
{
    unsigned int	offset;
    unsigned short	selector;
} __attribute__((packed)) INTDESC;
typedef struct
{
    unsigned int	offset32;
    unsigned short	selector;
} __attribute__((packed)) DPMI_INTDESC;

typedef struct segment_descriptor_s
{
    unsigned int	base_addr;	/* Pointer to segment in flat memory */
    unsigned int	limit;		/* Limit of Segment */
    unsigned int	type:2;
    unsigned int	is_32:1;	/* one for is 32-bit Segment */
    unsigned int	readonly:1;	/* one for read only Segments */
    unsigned int	is_big:1;	/* Granularity */
    unsigned int	not_present:1;
    unsigned int	useable:1;
    unsigned int	used;		/* Segment in use by client # */
					/* or Linux/GLibc (0xfe) */
					/* or DOSEMU (0xff) */
} SEGDESC;

struct sel_desc_s {
  unsigned short selector;
  unsigned int descriptor[2];
} __attribute__((packed));

struct RealModeCallStructure {
  unsigned int edi;
  unsigned int esi;
  unsigned int ebp;
  unsigned int esp_reserved;
  unsigned int ebx;
  unsigned int edx;
  unsigned int ecx;
  unsigned int eax;
  unsigned short flags;
  unsigned short es;
  unsigned short ds;
  unsigned short fs;
  unsigned short gs;
  unsigned short ip;
  unsigned short cs;
  unsigned short sp;
  unsigned short ss;
} __attribute__((packed));

typedef struct {
    unsigned short selector;
    unsigned int  offset;
    unsigned short rmreg_selector;
    unsigned int  rmreg_offset;
    struct RealModeCallStructure *rmreg;
    unsigned rm_ss_selector;
} RealModeCallBack;

struct DPMIclient_struct {
  struct sigcontext stack_frame;
  /* fpu_state needs to be paragraph aligned for fxrstor/fxsave */
  struct _fpstate fpu_state __attribute__ ((aligned(16)));
  int is_32;
  dpmi_pm_block_root *pm_block_root;
  unsigned short private_data_segment;
  int in_dpmi_rm_stack;
  unsigned char *pm_stack;
  int in_dpmi_pm_stack;
  /* for real mode call back, DPMI function 0x303 0x304 */
  RealModeCallBack realModeCallBack[0x10];
  Bit16u rmcb_seg;
  Bit16u rmcb_off;
  INTDESC Interrupt_Table[0x100];
  INTDESC Exception_Table[0x20];
  unsigned short LDT_ALIAS;
  unsigned short PMSTACK_SEL;	/* protected mode stack selector */
  /* used for RSP calls */
  unsigned short RSP_cs[DPMI_MAX_CLIENTS], RSP_ds[DPMI_MAX_CLIENTS];
  int RSP_state, RSP_installed;
};

struct RSPcall_s {
  unsigned char data16[8];
  unsigned char code16[8];
  unsigned short ip;
  unsigned short reserved;
  unsigned char data32[8];
  unsigned char code32[8];
  unsigned int eip;
};
struct RSP_s {
  struct RSPcall_s call;
  dpmi_pm_block_root *pm_block_root;
};

EXTERN volatile int in_dpmi INIT(0);/* Set to 1 when running under DPMI */
EXTERN volatile int in_dpmi_dos_int INIT(1);
EXTERN volatile int dpmi_mhp_TF INIT(0);
EXTERN unsigned char dpmi_mhp_intxxtab[256] INIT({0});
EXTERN volatile int is_cli INIT(0);

extern SEGDESC Segments[MAX_SELECTORS];
extern unsigned long dpmi_total_memory; /* total memory  of this session */
extern unsigned long dpmi_free_memory; /* how many bytes memory client */
				       /* can allocate */
extern unsigned long pm_block_handle_used;       /* tracking handle */
extern unsigned char *ldt_buffer;
extern unsigned char *ldt_alias;

void dpmi_get_entry_point(void);
#ifdef __x86_64__
extern void dpmi_iret_setup(struct sigcontext *);
#else
#define dpmi_iret_setup(x)
#endif
int indirect_dpmi_switch(struct sigcontext *);
#ifdef __linux__
int dpmi_fault(struct sigcontext *);
#endif
void dpmi_realmode_hlt(unsigned int);
void run_pm_int(int);
void run_pm_dos_int(int);
void fake_pm_int(void);
u_short DPMI_ldt_alias(void);

#ifdef __linux__
int dpmi_mhp_regs(void);
void dpmi_mhp_getcseip(unsigned int *seg, unsigned int *off);
void dpmi_mhp_getssesp(unsigned int *seg, unsigned int *off);
int dpmi_mhp_get_selector_size(int sel);
int dpmi_mhp_getcsdefault(void);
int dpmi_mhp_setTF(int on);
void dpmi_mhp_GetDescriptor(unsigned short selector, unsigned int *lp);
int dpmi_mhp_getselbase(unsigned short selector);
unsigned long dpmi_mhp_getreg(int regnum);
void dpmi_mhp_setreg(int regnum, unsigned long val);
void dpmi_mhp_modify_eip(int delta);
#endif

void add_cli_to_blacklist(void);
dpmi_pm_block DPMImalloc(unsigned long size);
dpmi_pm_block DPMImallocLinear(unsigned long base, unsigned long size, int committed);
int DPMIfree(unsigned long handle);
dpmi_pm_block DPMIrealloc(unsigned long handle, unsigned long size);
dpmi_pm_block DPMIreallocLinear(unsigned long handle, unsigned long size,
  int committed);
void DPMIfreeAll(void);
int DPMIMapConventionalMemory(unsigned long handle, unsigned long offset,
			  unsigned long low_addr, unsigned long cnt);
int DPMISetPageAttributes(unsigned long handle, int offs, u_short attrs[], int count);
int DPMIGetPageAttributes(unsigned long handle, int offs, u_short attrs[], int count);
void GetFreeMemoryInformation(unsigned int *lp);
int GetDescriptor(u_short selector, unsigned int *lp);
unsigned int GetSegmentBase(unsigned short);
unsigned int GetSegmentLimit(unsigned short);
int CheckSelectors(struct sigcontext *scp, int in_dosemu);
int ValidAndUsedSelector(unsigned short selector);
int dpmi_is_valid_range(dosaddr_t addr, int len);

extern char *DPMI_show_state(struct sigcontext *scp);
extern void dpmi_sigio(struct sigcontext *scp);
extern void run_dpmi(void);

extern int ConvertSegmentToDescriptor(unsigned short segment);
extern int ConvertSegmentToDescriptor_lim(unsigned short segment, unsigned long limit);
extern int ConvertSegmentToCodeDescriptor(unsigned short segment);
extern int ConvertSegmentToCodeDescriptor_lim(unsigned short segment, unsigned long limit);
extern int SetSegmentBaseAddress(unsigned short selector,
					unsigned long baseaddr);
extern int SetSegmentLimit(unsigned short, unsigned int);
extern DPMI_INTDESC dpmi_get_interrupt_vector(unsigned char num);
extern void dpmi_set_interrupt_vector(unsigned char num, DPMI_INTDESC desc);
extern void save_pm_regs(struct sigcontext *);
extern void restore_pm_regs(struct sigcontext *);
extern unsigned short AllocateDescriptors(int);
extern int SetSelector(unsigned short selector, dosaddr_t base_addr, unsigned int limit,
                       unsigned char is_32, unsigned char type, unsigned char readonly,
                       unsigned char is_big, unsigned char seg_not_present, unsigned char useable);
extern int FreeDescriptor(unsigned short selector);
extern void FreeSegRegs(struct sigcontext *scp, unsigned short selector);
extern far_t DPMI_allocate_realmode_callback(u_short sel, int offs, u_short rm_sel,
	int rm_offs);
extern int DPMI_free_realmode_callback(u_short seg, u_short off);

extern void dpmi_setup(void);
extern void dpmi_reset(void);
extern void dpmi_cleanup(void);
extern int get_ldt(void *buffer);
void dpmi_return_request(void);
void dpmi_return(struct sigcontext *scp, int retval);
int dpmi_check_return(struct sigcontext *scp);
void dpmi_init(void);
extern void copy_context(struct sigcontext *d,
    struct sigcontext *s, int copy_fpu);
extern unsigned short dpmi_sel(void);
extern unsigned short dpmi_data_sel(void);
//extern void pm_to_rm_regs(struct sigcontext *scp, unsigned int mask);
//extern void rm_to_pm_regs(struct sigcontext *scp, unsigned int mask);

static inline int DPMIValidSelector(unsigned short selector)
{
  /* does this selector refer to the LDT? */
  return Segments[selector >> 3].used != 0xfe && (selector & 4);
}

#endif /* DPMI_H */
