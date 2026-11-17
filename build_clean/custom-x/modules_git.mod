#  列         :  值说明
# "Module"    : 默认（除非指定Parent Dir列）在$PACKSHOP_WORK(定义在makepack_common.var中)下创建目录，名为该列所填的值。 用于保存从git服务器上下载的项目。
# "Src Path"  : 项目在git服务器上的路径名。
# "Commit No.": 项目的提交ID。如果为0或master、<分支名>，将分别下载主干和分支的最新版本，否则下载指定版本。
# "Parent Dir": 指定项目下载的本地目录，默认为$PACKSHOP_WORK(定义在makepack_common.var中)。

# 注意：${M_PLATFORM}: 是Master Platform简写。当几个平台有依赖关系时，它是代表平台。例如，HI3516DV300和HI3516CV500共用SDK，编译器相同，如果选HI3516CV500为代表，
#    则M_PLATFORM=HI3516CV500，PLATFORM=HI3516DV300，很多模块编译后都将提交到带有"HI3516CV500"项目中。
#    本文件中可用的变量目前暂只支持：${GIT_BASE}，${M_PLATFORM}，${PLATFORM}，这跟脚本使用本文件的位置有关。

# 建议：用UE（UltraEdit）编辑，空格就用空格键输入，不要用Tab键，因用BeyondCompare比较时，Tab键会产生和UE明显不同的空白，导致严重不对齐。

# Module          | Src Path                                                        | Commit No.         | Parent Dir
MakePack_Dir       | ${GIT_BASE}Product_IPC/${M_PLATFORM}.git                      | 33930e9d6fcf5052bd4efbca2078ee814d75ff75          | ..
Makepack_Scripts   | ${GIT_BASE}Packshop/MAKEPACK_SCRIPTS.git                      | 298fbdda16111ae39520708bc2bd831c67496d77          | $MY_DIR
Module_Cfgs_CICD   | ${GIT_BASE}Module_Cfgs${XM_CICD}/miniIPC_${M_PLATFORM}.git    | 8c22eda8c85459da9fd29195877292b562882287          | $MY_DIR
Common_Cfgs        | ${GIT_BASE}Packshop/COMMON_CFGS_NEW.git                       | c3d605fcac7f4b05d2c79b446ff3f35580249fd1          | $MY_DIR
Json_Files         | ${GIT_BASE}Json_Files/${M_PLATFORM}.git                       | 83b2e1b2143e8b12b5b04a7d8a5b5f175a72fbef          | $MY_DIR
video_params       | ${GIT_BASE}Video_Params/${M_PLATFORM}.git                     | e401da382ec52a014e3687b0e7f8ca442c2d4697          | $MY_DIR
OpenSource         | ${GIT_BASE}OpenSource_Tags${XM_CICD}/${M_PLATFORM}.git        | c71c2e4323bcdb6ee10b505c6a4bda8a97ac1b46          | 0
Tools_PC           | ${GIT_BASE}Packshop/TOOLS_PC.git                              | 77e3a00a85262efd9b1f32f75b6a6a055c4906d0          | 0
XmTools_PC         | ${GIT_BASE}PC_Tags/P_XMTOOLS_PC_20181103.git                  | f502afeb89b2e3695855d95a0d796d69b0891864          | 0
PlatformShop       | ${GIT_BASE}Packshop/${M_PLATFORM}.git                         | f7d65f737e79f732893b9e95bcafd952cf8040b5          | 0
XmDrv_Tags         | ${GIT_BASE}XmDrv_Tags${XM_CICD}/${M_PLATFORM}.git             | 95a12589330a4031dfac73737d82d4b875447e7b          | 0
Linux_Tags         | ${GIT_BASE}Linux_Tags${XM_CICD}/${M_PLATFORM}.git             | b93d6880852d3d1c38e02b96e1ba058944c7b0ba          | 0
LibXmDvr_Tags      | ${GIT_BASE}LibXmDvr_Tags${XM_CICD}/${M_PLATFORM}.git          | 17f2f974e037c4c2cb3ff375248d86fe586cd0de          | 0
LibXmCap_Tags      | ${GIT_BASE}LibXmCap_Tags${XM_CICD}/${M_PLATFORM}.git          | 0e6f38a53c093e06935f6afacea24abc880d4f73          | 0
LibXmIsp_Tags      | ${GIT_BASE}LibXmIsp_Tags${XM_CICD}/${M_PLATFORM}.git          | b1a9330d5293645841379c6643de20b3352d0156          | 0
LibXmImp_Tags      | ${GIT_BASE}LibXmImp_Tags${XM_CICD}/${M_PLATFORM}.git          | 131c5bf18941039880094c0a61aa9ea8910176d9_G2-WR-V  | 0
AI_Algo            | ${GIT_BASE}AI_Algo/${M_PLATFORM}.git                          | 7bd4c46e1092f715d68eb92a30068a126f8f1bd6          | 0
Wifi_Tags          | ${GIT_BASE}Wifi_Tags${XM_CICD}/${M_PLATFORM}.git              | 378723427b6907e6cc3f87d90d040cac5959595b          | 0
Wifi_Scripts       | ${GIT_BASE}Wifi_Tags/WIFI_SCRIPTS.git                         | 6a9aa85688da752fec054efdb422683937fd4054          | 0
Api_Include        | ${GIT_BASE}API_Include/P_API_INCLUDE_20240118.git             | e06ac81cb211dc8271f03272ad66a50fa7536595          | 0
LibXmVideo_Params  | ${GIT_BASE}miniIPCBuilder/P_LIBXMVIDEO_PARAM_GK_20210525.git  | c4f7c6e725d5349f42d4d78e0ef44dc06f2d3142          | 0
Functions          | ${GIT_BASE}miniIPC_libs_${M_PLATFORM}/Functions.git           | febb595c01d02a1fa3662fd9ec16952dd25a2cc3_C01      | 0
XmClouds           | ${GIT_BASE}miniIPC_libs_${M_PLATFORM}/XmClouds.git            | 4b986c9dd184436ce03fcaa9780188ccb9ae2fc7_C01      | 0
RTSPs              | ${GIT_BASE}RTSPs${XM_CICD}/${M_PLATFORM}.git                  | f585abf36be303604617d149bd550df1423b99e3_C01      | 0
NetServices        | ${GIT_BASE}NetServices${XM_CICD}/${M_PLATFORM}.git            | be1b896e84c3c2187bf7d040c93b0b0ee2b27dc2_C01      | 0
LibXmV3            | ${GIT_BASE}LibXmV3${XM_CICD}/${M_PLATFORM}.git                | a2322d49cea272247683743cf76cbcae999abb1a_C01      | 0
LibXmCloud         | ${GIT_BASE}LibXmCloud${XM_CICD}/${M_PLATFORM}.git             | 706257a776e62abcb14e1375f4ecc80feccf10a8_C01      | 0
LibRecords         | ${GIT_BASE}LibRecords${XM_CICD}/${M_PLATFORM}.git             | e13185c84ea9153d4be3e76a05deebc160096286_C01      | 0
LibMedia           | ${GIT_BASE}LibMedia${XM_CICD}/${M_PLATFORM}.git               | 6b090f9cc360ddb0ab381aa5280a3e1f32e83069_C01      | 0
LibManager         | ${GIT_BASE}LibManager${XM_CICD}/${M_PLATFORM}.git             | 702e7fd716c10b53aaddc24c4579118e27d47c98_C01      | 0
LibLocalSDK        | ${GIT_BASE}LibLocalSDK${XM_CICD}/${M_PLATFORM}.git            | 6961c92c055580023f8c6f380160b6b69dbe6dab_C01      | 0
LibExtNetProtocol  | ${GIT_BASE}LibExtNetProtocol${XM_CICD}/${M_PLATFORM}.git      | e75f8a1acaaf23a242ff671fdb05f51928cfc76c_C01      | 0
LibComm            | ${GIT_BASE}LibComm${XM_CICD}/${M_PLATFORM}.git                | 78319c09796f332a9dab2696b7184154d842d723_C01      | 0
LibAlarm           | ${GIT_BASE}LibAlarm${XM_CICD}/${M_PLATFORM}.git               | 4c2b80c14f19ae3d1aaeb0f4ff7ed108cbf00eb8_C01      | 0
Infras             | ${GIT_BASE}Infras${XM_CICD}/${M_PLATFORM}.git                 | d2fbd046e228c39b98e7722cb0ed976d0faaacc2_C01      | 0
LowPower           | ${GIT_BASE}LowPower${XM_CICD}/${M_PLATFORM}.git               | 93434426a81952de1371cbe4d405cf873e8f1f16_C01      | 0
_include           | ${GIT_BASE}miniIPCBuilder/_include.git                        | d7867c38fe0eb53d751f4b36cbb0656a91710a19_C01      | 0
MainApplication    | ${GIT_BASE}miniIPCBuilder/MainApplication.git                 | bca8bdf814d8e591c0c8963e09b4f19e173edd0b_C01      | 0
Common             | ${GIT_BASE}miniIPCBuilder/Commom.git                          | 73f524c95d14a257293bc459c6428ab2a937bdec          | 0
Web                | ${GIT_BASE}miniIPCBuilder/Web.git                             | 85946dfcfb36761f9c5f1823bb35c2fe8d15c44f          | 0
NetworkAll         | ${GIT_BASE}NetworkAll${XM_CICD}/${M_PLATFORM}.git             | 4eb2cb63d7aaf3abccd264c2b21ca3ef5bdf35bb          | 0
MiniApp            | ${GIT_BASE}miniIPCBuilder/P_MINIAPP_20240122.git              | 2f583bda55333c0ea1ba6d4b109b2fd8963d164c          | 0
Make_Initrd        | ${GIT_BASE}miniIPCBuilder/Make_Initrd_20240120.git            | aeed23928a3231ca29d9c54c50c7431457ddbfcc          | 0
