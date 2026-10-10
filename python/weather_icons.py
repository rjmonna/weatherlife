"""Icon-name to wire index, recovered from usbwr.exe FUN_00410950."""

UNKNOWN_ICON = 79

_TABLE_A = (
    "SO____", "MS____", "WB____", "BD____", "BW____", "NB____",
    "NS____", "BDR1__", "BWR1__", "WBR1__", "BDS1__", "BWS1__",
    "WBS1__", "BDR2__", "BWR2__", "WBR2__", "BDS2__", "BWS2__",
    "WBS2__", "BDR3__", "BWR3__", "WBR3__", "BDS3__", "BWS3__",
    "WBS3__", "BDSR1_", "BWSR1_", "WBSR1_", "BDSRS1", "BWSRS1",
    "WBSRS1", "BDSR2_", "BWSR2_", "WBSR2_", "BDSRS2", "BWSRS2",
    "WBSRS2", "BDSR3_", "BWSR3_", "WBSR3_", "BDSRS3", "BWSRS3",
    "WBSRS3", "BDSN1_", "BWSN1_", "WBSN1_", "BDSNS1", "BWSNS1",
    "WBSNS1", "BDSN2_", "BWSN2_", "WBSN2_", "BDSNS2", "BWSNS2",
    "WBSNS2", "BDSN3_", "BWSN3_", "WBSN3_", "BDSNS3", "BWSNS3",
    "WBSNS3", "BDSG__", "BWSG__", "WBSG__", "BDG1__", "BWG1__",
    "WBG1__", "BDG2__", "BWG2__", "WBG2__", "BDG3__", "BWG3__",
    "WBG3__", "BDGR1_", "BWGR1_", "WBGR1_", "BDGR2_", "BWGR2_",
    "WBGR2_",
)

_TABLE_B = (
    "MO____", "MM____", "MB____", "MD____", "MW____", "NB____",
    "NM____", "MDR1__", "MWR1__", "MBR1__", "MDS1__", "MWS1__",
    "MBS1__", "MDR2__", "MWR2__", "MBR2__", "MDS2__", "MWS2__",
    "MBS2__", "MDR3__", "MWR3__", "MBR3__", "MDS3__", "MWS3__",
    "MBS3__", "MDSR1_", "MWSR1_", "MBSR1_", "MDSRS1", "MWSRS1",
    "MBSRS1", "MDSR2_", "MWSR2_", "MBSR2_", "MDSRS2", "MWSRS2",
    "MBSRS2", "MDSR3_", "MWSR3_", "MBSR3_", "MDSRS3", "MWSRS3",
    "MBSRS3", "MDSN1_", "MWSN1_", "MBSN1_", "MDSNS1", "MWSNS1",
    "MBSNS1", "MDSN2_", "MWSN2_", "MBSN2_", "MDSNS2", "MWSNS2",
    "MBSNS2", "MDSN3_", "MWSN3_", "MBSN3_", "MDSNS3", "MWSNS3",
    "MBSNS3", "MDSG__", "MWSG__", "MBSG__", "MDG1__", "MWG1__",
    "MBG1__", "MDG2__", "MWG2__", "MBG2__", "MDG3__", "MWG3__",
    "MBG3__", "MDGR1_", "MWGR1_", "MBGR1_", "MDGR2_", "MWGR2_",
    "MBGR2_",
)


def icon_index(name: str) -> int:
    """Return the 7-bit icon index for an uppercase legacy name, or 79."""
    for table in (_TABLE_A, _TABLE_B):
        if name in table:
            return table.index(name)
    return UNKNOWN_ICON
