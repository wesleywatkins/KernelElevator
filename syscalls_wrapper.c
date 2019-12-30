#include <linux/linkage.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/export.h>

/* System call stubs */
long (*STUB_start_elevator)(void) = NULL;
long (*STUB_issue_request)(int,int,int) = NULL;
long (*STUB_stop_elevator)(void) = NULL;
EXPORT_SYMBOL(STUB_start_elevator);
EXPORT_SYMBOL(STUB_issue_request);
EXPORT_SYMBOL(STUB_stop_elevator);

/* System call wrapper */
asmlinkage long sys_start_elevator(void) {
	if (STUB_start_elevator != NULL)
		return STUB_start_elevator();
	else
		return -ENOSYS;
}

/* System call wrapper */
asmlinkage long sys_issue_request(int passenger_type, int start_floor, int destination_floor) {
	if (STUB_issue_request != NULL)
		return STUB_issue_request(passenger_type,start_floor,destination_floor);
	else
		return -ENOSYS;
}

/* System call wrapper */
asmlinkage long sys_stop_elevator(void) {
	if (STUB_stop_elevator != NULL)
		return STUB_stop_elevator();
	else
		return -ENOSYS;
}