#include "ubin.h"
#include "vfs.h"
#include "util.h"

extern int ush_main(int argc, char **argv);
extern int uls_main(int argc, char **argv);
extern int ucat_main(int argc, char **argv);
extern int uedit_main(int argc, char **argv);
extern int upeak_main(int argc, char **argv);
extern int upwd_main(int argc, char **argv);
extern int ucd_main(int argc, char **argv);
extern int umkdir_main(int argc, char **argv);
extern int utouch_main(int argc, char **argv);
extern int urm_main(int argc, char **argv);
extern int ucp_main(int argc, char **argv);
extern int umv_main(int argc, char **argv);
extern int uln_main(int argc, char **argv);
extern int uchmod_main(int argc, char **argv);
extern int ureadlink_main(int argc, char **argv);
extern int ustat_main(int argc, char **argv);
extern int udu_main(int argc, char **argv);
extern int udf_main(int argc, char **argv);
extern int utruncate_main(int argc, char **argv);
extern int uhead_main(int argc, char **argv);
extern int utail_main(int argc, char **argv);
extern int uwc_main(int argc, char **argv);
extern int ugrep_main(int argc, char **argv);
extern int udiff_main(int argc, char **argv);
extern int usort_main(int argc, char **argv);
extern int uuniq_main(int argc, char **argv);
extern int ucut_main(int argc, char **argv);
extern int utr_main(int argc, char **argv);
extern int used_main(int argc, char **argv);
extern int ucmp_main(int argc, char **argv);
extern int ubasename_main(int argc, char **argv);
extern int udirname_main(int argc, char **argv);
extern int urealpath_main(int argc, char **argv);
extern int uhexdump_main(int argc, char **argv);
extern int ustrings_main(int argc, char **argv);
extern int uecho_main(int argc, char **argv);
extern int uprintf_main(int argc, char **argv);
extern int utee_main(int argc, char **argv);
extern int utest_main(int argc, char **argv);
extern int uyes_main(int argc, char **argv);
extern int ufold_main(int argc, char **argv);
extern int urev_main(int argc, char **argv);
extern int uod_main(int argc, char **argv);
extern int usplit_main(int argc, char **argv);
extern int upaste_main(int argc, char **argv);
extern int unl_main(int argc, char **argv);
extern int utac_main(int argc, char **argv);
extern int uxargs_main(int argc, char **argv);
extern int uawk_main(int argc, char **argv);
extern int ujq_main(int argc, char **argv);
extern int usha256sum_main(int argc, char **argv);
extern int umd5sum_main(int argc, char **argv);
extern int ubase64_main(int argc, char **argv);
extern int uless_main(int argc, char **argv);
extern int umore_main(int argc, char **argv);
extern int utime_main(int argc, char **argv);
extern int uclear_main(int argc, char **argv);
extern int utree_main(int argc, char **argv);
extern int ufind_main(int argc, char **argv);
extern int udate_main(int argc, char **argv);
extern int ufree_main(int argc, char **argv);
extern int uenv_main(int argc, char **argv);
extern int uexport_main(int argc, char **argv);
extern int uwhich_main(int argc, char **argv);
extern int useq_main(int argc, char **argv);
extern int usleep_main(int argc, char **argv);
extern int uhostname_main(int argc, char **argv);
extern int uuptime_main(int argc, char **argv);
extern int uwhoami_main(int argc, char **argv);
extern int uid_main(int argc, char **argv);
extern int ucal_main(int argc, char **argv);
extern int ugzip_main(int argc, char **argv);
extern int ugunzip_main(int argc, char **argv);
extern int utimeout_main(int argc, char **argv);
extern int uwatch_main(int argc, char **argv);
extern int utheme_main(int argc, char **argv);
extern int uwallpaper_main(int argc, char **argv);
extern int uscale_main(int argc, char **argv);
extern int uhelp_main(int argc, char **argv);
extern int uhistory_main(int argc, char **argv);
extern int ualias_main(int argc, char **argv);
extern int uman_main(int argc, char **argv);
extern int uask_main(int argc, char **argv);
extern int uaudit_main(int argc, char **argv);
extern int umemory_main(int argc, char **argv);
extern int upeakvec_main(int argc, char **argv);
extern int upolicy_main(int argc, char **argv);
extern int uprivacy_main(int argc, char **argv);
extern int udisksave_main(int argc, char **argv);
extern int ugui_main(int argc, char **argv);
extern int uuname_main(int argc, char **argv);
extern int utrue_main(int argc, char **argv);
extern int ufalse_main(int argc, char **argv);
extern int ureboot_main(int argc, char **argv);
extern int uctr_main(int argc, char **argv);
extern int uctrd_main(int argc, char **argv);
extern int uifconfig_main(int argc, char **argv);
extern int uping_main(int argc, char **argv);
extern int uwget_main(int argc, char **argv);
extern int ucurl_main(int argc, char **argv);
extern int unslookup_main(int argc, char **argv);
extern int uhost_main(int argc, char **argv);
extern int utraceroute_main(int argc, char **argv);
extern int unc_main(int argc, char **argv);
extern int utlsinfo_main(int argc, char **argv);
extern int utar_main(int argc, char **argv);
extern int uzip_main(int argc, char **argv);
extern int uunzip_main(int argc, char **argv);
extern int utop_main(int argc, char **argv);
extern int usysmon_main(int argc, char **argv);
extern int ups_main(int argc, char **argv);
extern int ukill_main(int argc, char **argv);
extern int ujs_main(int argc, char **argv);

static const struct ubin_entry ubin_table[] = {
#define UBIN_CMD(name, fn) { name, fn },
#include "ubin_cmds.def"
#undef UBIN_CMD
    { NULL, NULL }
};

void ubin_seed_vfs(void) {
    for (int i = 0; ubin_table[i].name; i++) {
        char path[64];
        snprintf(path, sizeof(path), "/bin/%s", ubin_table[i].name);
        (void)vfs_write_file(path, "PEAKBUILTIN", 11);
    }
}

int ubin_run(const char *path, int argc, char **argv) {
    for (int i = 0; ubin_table[i].name; i++) {
        char expect[64];
        snprintf(expect, sizeof(expect), "/bin/%s", ubin_table[i].name);
        if (!strcmp(path, expect))
            return ubin_table[i].main(argc, argv);
    }
    return -999;
}
