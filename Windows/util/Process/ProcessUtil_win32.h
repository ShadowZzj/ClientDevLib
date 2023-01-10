#ifndef ProcessUtil_win32_h
#define ProcessUtil_win32_h

#include "ProcessKernelStruct_win32.h"

namespace _SELF_
{
	typedef enum _PROCESSINFOCLASS_SELF_
	{
		ProcessBasicInformation, // q: PROCESS_BASIC_INFORMATION, PROCESS_EXTENDED_BASIC_INFORMATION
		ProcessQuotaLimits, // qs: QUOTA_LIMITS, QUOTA_LIMITS_EX
		ProcessIoCounters, // q: IO_COUNTERS
		ProcessVmCounters, // q: VM_COUNTERS, VM_COUNTERS_EX, VM_COUNTERS_EX2
		ProcessTimes, // q: KERNEL_USER_TIMES
		ProcessBasePriority, // s: KPRIORITY
		ProcessRaisePriority, // s: ULONG
		ProcessDebugPort, // q: HANDLE
		ProcessExceptionPort, // s: PROCESS_EXCEPTION_PORT
		ProcessAccessToken, // s: PROCESS_ACCESS_TOKEN
		ProcessLdtInformation, // qs: PROCESS_LDT_INFORMATION // 10
		ProcessLdtSize, // s: PROCESS_LDT_SIZE
		ProcessDefaultHardErrorMode, // qs: ULONG
		ProcessIoPortHandlers, // (kernel-mode only)
		ProcessPooledUsageAndLimits, // q: POOLED_USAGE_AND_LIMITS
		ProcessWorkingSetWatch, // q: PROCESS_WS_WATCH_INFORMATION[]; s: void
		ProcessUserModeIOPL,
		ProcessEnableAlignmentFaultFixup, // s: BOOLEAN
		ProcessPriorityClass, // qs: PROCESS_PRIORITY_CLASS
		ProcessWx86Information,
		ProcessHandleCount, // q: ULONG, PROCESS_HANDLE_INFORMATION // 20
		ProcessAffinityMask, // s: KAFFINITY
		ProcessPriorityBoost, // qs: ULONG
		ProcessDeviceMap, // qs: PROCESS_DEVICEMAP_INFORMATION, PROCESS_DEVICEMAP_INFORMATION_EX
		ProcessSessionInformation, // q: PROCESS_SESSION_INFORMATION
		ProcessForegroundInformation, // s: PROCESS_FOREGROUND_BACKGROUND
		ProcessWow64Information, // q: ULONG_PTR
		ProcessImageFileName, // q: UNICODE_STRING
		ProcessLUIDDeviceMapsEnabled, // q: ULONG
		ProcessBreakOnTermination, // qs: ULONG
		ProcessDebugObjectHandle, // q: HANDLE // 30
		ProcessDebugFlags, // qs: ULONG
		ProcessHandleTracing, // q: PROCESS_HANDLE_TRACING_QUERY; s: size 0 disables, otherwise enables
		ProcessIoPriority, // qs: IO_PRIORITY_HINT
		ProcessExecuteFlags, // qs: ULONG
		ProcessResourceManagement, // ProcessTlsInformation // PROCESS_TLS_INFORMATION
		ProcessCookie, // q: ULONG
		ProcessImageInformation, // q: SECTION_IMAGE_INFORMATION
		ProcessCycleTime, // q: PROCESS_CYCLE_TIME_INFORMATION // since VISTA
		ProcessPagePriority, // q: PAGE_PRIORITY_INFORMATION
		ProcessInstrumentationCallback, // qs: PROCESS_INSTRUMENTATION_CALLBACK_INFORMATION // 40
		ProcessThreadStackAllocation, // s: PROCESS_STACK_ALLOCATION_INFORMATION, PROCESS_STACK_ALLOCATION_INFORMATION_EX
		ProcessWorkingSetWatchEx, // q: PROCESS_WS_WATCH_INFORMATION_EX[]
		ProcessImageFileNameWin32, // q: UNICODE_STRING
		ProcessImageFileMapping, // q: HANDLE (input)
		ProcessAffinityUpdateMode, // qs: PROCESS_AFFINITY_UPDATE_MODE
		ProcessMemoryAllocationMode, // qs: PROCESS_MEMORY_ALLOCATION_MODE
		ProcessGroupInformation, // q: USHORT[]
		ProcessTokenVirtualizationEnabled, // s: ULONG
		ProcessConsoleHostProcess, // q: ULONG_PTR // ProcessOwnerInformation
		ProcessWindowInformation, // q: PROCESS_WINDOW_INFORMATION // 50
		ProcessHandleInformation, // q: PROCESS_HANDLE_SNAPSHOT_INFORMATION // since WIN8
		ProcessMitigationPolicy, // s: PROCESS_MITIGATION_POLICY_INFORMATION
		ProcessDynamicFunctionTableInformation,
		ProcessHandleCheckingMode, // qs: ULONG; s: 0 disables, otherwise enables
		ProcessKeepAliveCount, // q: PROCESS_KEEPALIVE_COUNT_INFORMATION
		ProcessRevokeFileHandles, // s: PROCESS_REVOKE_FILE_HANDLES_INFORMATION
		ProcessWorkingSetControl, // s: PROCESS_WORKING_SET_CONTROL
		ProcessHandleTable, // since WINBLUE
		ProcessCheckStackExtentsMode,
		ProcessCommandLineInformation, // q: UNICODE_STRING // 60
		ProcessProtectionInformation, // q: PS_PROTECTION
		ProcessMemoryExhaustion, // PROCESS_MEMORY_EXHAUSTION_INFO // since THRESHOLD
		ProcessFaultInformation, // PROCESS_FAULT_INFORMATION
		ProcessTelemetryIdInformation, // PROCESS_TELEMETRY_ID_INFORMATION
		ProcessCommitReleaseInformation, // PROCESS_COMMIT_RELEASE_INFORMATION
		ProcessDefaultCpuSetsInformation,
		ProcessAllowedCpuSetsInformation,
		ProcessSubsystemProcess,
		ProcessJobMemoryInformation, // PROCESS_JOB_MEMORY_INFO
		ProcessInPrivate, // since THRESHOLD2 // 70
		ProcessRaiseUMExceptionOnInvalidHandleClose,
		ProcessIumChallengeResponse,
		ProcessChildProcessInformation, // PROCESS_CHILD_PROCESS_INFORMATION
		ProcessHighGraphicsPriorityInformation,
		ProcessSubsystemInformation, // q: SUBSYSTEM_INFORMATION_TYPE // since REDSTONE2
		ProcessEnergyValues, // PROCESS_ENERGY_VALUES, PROCESS_EXTENDED_ENERGY_VALUES
		ProcessActivityThrottleState, // PROCESS_ACTIVITY_THROTTLE_STATE
		ProcessActivityThrottlePolicy, // PROCESS_ACTIVITY_THROTTLE_POLICY
		ProcessWin32kSyscallFilterInformation,
		ProcessDisableSystemAllowedCpuSets, // 80
		ProcessWakeInformation, // PROCESS_WAKE_INFORMATION
		ProcessEnergyTrackingState, // PROCESS_ENERGY_TRACKING_STATE
		ProcessManageWritesToExecutableMemory, // MANAGE_WRITES_TO_EXECUTABLE_MEMORY // since REDSTONE3
		ProcessCaptureTrustletLiveDump,
		ProcessTelemetryCoverage,
		ProcessEnclaveInformation,
		ProcessEnableReadWriteVmLogging, // PROCESS_READWRITEVM_LOGGING_INFORMATION
		ProcessUptimeInformation, // PROCESS_UPTIME_INFORMATION
		ProcessImageSection,
		ProcessDebugAuthInformation, // since REDSTONE4 // 90
		ProcessSystemResourceManagement, // PROCESS_SYSTEM_RESOURCE_MANAGEMENT
		ProcessSequenceNumber, // q: ULONGLONG
		ProcessLoaderDetour, // since REDSTONE5
		ProcessSecurityDomainInformation, // PROCESS_SECURITY_DOMAIN_INFORMATION
		ProcessCombineSecurityDomainsInformation, // PROCESS_COMBINE_SECURITY_DOMAINS_INFORMATION
		ProcessEnableLogging, // PROCESS_LOGGING_INFORMATION
		ProcessLeapSecondInformation, // PROCESS_LEAP_SECOND_INFORMATION
		MaxProcessInfoClass
	} PROCESSINFOCLASS_SELF;
	typedef enum _SYSTEM_INFORMATION_CLASS
	{
		SystemBasicInformation, // q: SYSTEM_BASIC_INFORMATION
		SystemProcessorInformation, // q: SYSTEM_PROCESSOR_INFORMATION
		SystemPerformanceInformation, // q: SYSTEM_PERFORMANCE_INFORMATION
		SystemTimeOfDayInformation, // q: SYSTEM_TIMEOFDAY_INFORMATION
		SystemPathInformation, // not implemented
		SystemProcessInformation, // q: SYSTEM_PROCESS_INFORMATION
		SystemCallCountInformation, // q: SYSTEM_CALL_COUNT_INFORMATION
		SystemDeviceInformation, // q: SYSTEM_DEVICE_INFORMATION
		SystemProcessorPerformanceInformation, // q: SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION
		SystemFlagsInformation, // q: SYSTEM_FLAGS_INFORMATION
		SystemCallTimeInformation, // not implemented // SYSTEM_CALL_TIME_INFORMATION // 10
		SystemModuleInformation, // q: RTL_PROCESS_MODULES
		SystemLocksInformation, // q: RTL_PROCESS_LOCKS
		SystemStackTraceInformation, // q: RTL_PROCESS_BACKTRACES
		SystemPagedPoolInformation, // not implemented
		SystemNonPagedPoolInformation, // not implemented
		SystemHandleInformation, // q: SYSTEM_HANDLE_INFORMATION
		SystemObjectInformation, // q: SYSTEM_OBJECTTYPE_INFORMATION mixed with SYSTEM_OBJECT_INFORMATION
		SystemPageFileInformation, // q: SYSTEM_PAGEFILE_INFORMATION
		SystemVdmInstemulInformation, // q
		SystemVdmBopInformation, // not implemented // 20
		SystemFileCacheInformation, // q: SYSTEM_FILECACHE_INFORMATION; s (requires SeIncreaseQuotaPrivilege) (info for WorkingSetTypeSystemCache)
		SystemPoolTagInformation, // q: SYSTEM_POOLTAG_INFORMATION
		SystemInterruptInformation, // q: SYSTEM_INTERRUPT_INFORMATION
		SystemDpcBehaviorInformation, // q: SYSTEM_DPC_BEHAVIOR_INFORMATION; s: SYSTEM_DPC_BEHAVIOR_INFORMATION (requires SeLoadDriverPrivilege)
		SystemFullMemoryInformation, // not implemented
		SystemLoadGdiDriverInformation, // s (kernel-mode only)
		SystemUnloadGdiDriverInformation, // s (kernel-mode only)
		SystemTimeAdjustmentInformation, // q: SYSTEM_QUERY_TIME_ADJUST_INFORMATION; s: SYSTEM_SET_TIME_ADJUST_INFORMATION (requires SeSystemtimePrivilege)
		SystemSummaryMemoryInformation, // not implemented
		SystemMirrorMemoryInformation, // s (requires license value "Kernel-MemoryMirroringSupported") (requires SeShutdownPrivilege) // 30
		SystemPerformanceTraceInformation, // q; s: (type depends on EVENT_TRACE_INFORMATION_CLASS)
		SystemObsolete0, // not implemented
		SystemExceptionInformation, // q: SYSTEM_EXCEPTION_INFORMATION
		SystemCrashDumpStateInformation, // s (requires SeDebugPrivilege)
		SystemKernelDebuggerInformation, // q: SYSTEM_KERNEL_DEBUGGER_INFORMATION
		SystemContextSwitchInformation, // q: SYSTEM_CONTEXT_SWITCH_INFORMATION
		SystemRegistryQuotaInformation, // q: SYSTEM_REGISTRY_QUOTA_INFORMATION; s (requires SeIncreaseQuotaPrivilege)
		SystemExtendServiceTableInformation, // s (requires SeLoadDriverPrivilege) // loads win32k only
		SystemPrioritySeperation, // s (requires SeTcbPrivilege)
		SystemVerifierAddDriverInformation, // s (requires SeDebugPrivilege) // 40
		SystemVerifierRemoveDriverInformation, // s (requires SeDebugPrivilege)
		SystemProcessorIdleInformation, // q: SYSTEM_PROCESSOR_IDLE_INFORMATION
		SystemLegacyDriverInformation, // q: SYSTEM_LEGACY_DRIVER_INFORMATION
		SystemCurrentTimeZoneInformation, // q; s: RTL_TIME_ZONE_INFORMATION
		SystemLookasideInformation, // q: SYSTEM_LOOKASIDE_INFORMATION
		SystemTimeSlipNotification, // s (requires SeSystemtimePrivilege)
		SystemSessionCreate, // not implemented
		SystemSessionDetach, // not implemented
		SystemSessionInformation, // not implemented (SYSTEM_SESSION_INFORMATION)
		SystemRangeStartInformation, // q: SYSTEM_RANGE_START_INFORMATION // 50
		SystemVerifierInformation, // q: SYSTEM_VERIFIER_INFORMATION; s (requires SeDebugPrivilege)
		SystemVerifierThunkExtend, // s (kernel-mode only)
		SystemSessionProcessInformation, // q: SYSTEM_SESSION_PROCESS_INFORMATION
		SystemLoadGdiDriverInSystemSpace, // s (kernel-mode only) (same as SystemLoadGdiDriverInformation)
		SystemNumaProcessorMap, // q
		SystemPrefetcherInformation, // q: PREFETCHER_INFORMATION; s: PREFETCHER_INFORMATION // PfSnQueryPrefetcherInformation
		SystemExtendedProcessInformation, // q: SYSTEM_PROCESS_INFORMATION
		SystemRecommendedSharedDataAlignment, // q
		SystemComPlusPackage, // q; s
		SystemNumaAvailableMemory, // 60
		SystemProcessorPowerInformation, // q: SYSTEM_PROCESSOR_POWER_INFORMATION
		SystemEmulationBasicInformation,
		SystemEmulationProcessorInformation,
		SystemExtendedHandleInformation, // q: SYSTEM_HANDLE_INFORMATION_EX
		SystemLostDelayedWriteInformation, // q: ULONG
		SystemBigPoolInformation, // q: SYSTEM_BIGPOOL_INFORMATION
		SystemSessionPoolTagInformation, // q: SYSTEM_SESSION_POOLTAG_INFORMATION
		SystemSessionMappedViewInformation, // q: SYSTEM_SESSION_MAPPED_VIEW_INFORMATION
		SystemHotpatchInformation, // q; s: SYSTEM_HOTPATCH_CODE_INFORMATION
		SystemObjectSecurityMode, // q: ULONG // 70
		SystemWatchdogTimerHandler, // s (kernel-mode only)
		SystemWatchdogTimerInformation, // q (kernel-mode only); s (kernel-mode only)
		SystemLogicalProcessorInformation, // q: SYSTEM_LOGICAL_PROCESSOR_INFORMATION
		SystemWow64SharedInformationObsolete, // not implemented
		SystemRegisterFirmwareTableInformationHandler, // s (kernel-mode only)
		SystemFirmwareTableInformation, // SYSTEM_FIRMWARE_TABLE_INFORMATION
		SystemModuleInformationEx, // q: RTL_PROCESS_MODULE_INFORMATION_EX
		SystemVerifierTriageInformation, // not implemented
		SystemSuperfetchInformation, // q; s: SUPERFETCH_INFORMATION // PfQuerySuperfetchInformation
		SystemMemoryListInformation, // q: SYSTEM_MEMORY_LIST_INFORMATION; s: SYSTEM_MEMORY_LIST_COMMAND (requires SeProfileSingleProcessPrivilege) // 80
		SystemFileCacheInformationEx, // q: SYSTEM_FILECACHE_INFORMATION; s (requires SeIncreaseQuotaPrivilege) (same as SystemFileCacheInformation)
		SystemThreadPriorityClientIdInformation, // s: SYSTEM_THREAD_CID_PRIORITY_INFORMATION (requires SeIncreaseBasePriorityPrivilege)
		SystemProcessorIdleCycleTimeInformation, // q: SYSTEM_PROCESSOR_IDLE_CYCLE_TIME_INFORMATION[]
		SystemVerifierCancellationInformation, // not implemented // name:wow64:whNT32QuerySystemVerifierCancellationInformation
		SystemProcessorPowerInformationEx, // not implemented
		SystemRefTraceInformation, // q; s: SYSTEM_REF_TRACE_INFORMATION // ObQueryRefTraceInformation
		SystemSpecialPoolInformation, // q; s (requires SeDebugPrivilege) // MmSpecialPoolTag, then MmSpecialPoolCatchOverruns != 0
		SystemProcessIdInformation, // q: SYSTEM_PROCESS_ID_INFORMATION
		SystemErrorPortInformation, // s (requires SeTcbPrivilege)
		SystemBootEnvironmentInformation, // q: SYSTEM_BOOT_ENVIRONMENT_INFORMATION // 90
		SystemHypervisorInformation, // q; s (kernel-mode only)
		SystemVerifierInformationEx, // q; s: SYSTEM_VERIFIER_INFORMATION_EX
		SystemTimeZoneInformation, // s (requires SeTimeZonePrivilege)
		SystemImageFileExecutionOptionsInformation, // s: SYSTEM_IMAGE_FILE_EXECUTION_OPTIONS_INFORMATION (requires SeTcbPrivilege)
		SystemCoverageInformation, // q; s // name:wow64:whNT32QuerySystemCoverageInformation; ExpCovQueryInformation
		SystemPrefetchPatchInformation, // not implemented
		SystemVerifierFaultsInformation, // s (requires SeDebugPrivilege)
		SystemSystemPartitionInformation, // q: SYSTEM_SYSTEM_PARTITION_INFORMATION
		SystemSystemDiskInformation, // q: SYSTEM_SYSTEM_DISK_INFORMATION
		SystemProcessorPerformanceDistribution, // q: SYSTEM_PROCESSOR_PERFORMANCE_DISTRIBUTION // 100
		SystemNumaProximityNodeInformation,
		SystemDynamicTimeZoneInformation, // q; s (requires SeTimeZonePrivilege)
		SystemCodeIntegrityInformation, // q: SYSTEM_CODEINTEGRITY_INFORMATION // SeCodeIntegrityQueryInformation
		SystemProcessorMicrocodeUpdateInformation, // s
		SystemProcessorBrandString, // q // HaliQuerySystemInformation -> HalpGetProcessorBrandString, info class 23
		SystemVirtualAddressInformation, // q: SYSTEM_VA_LIST_INFORMATION[]; s: SYSTEM_VA_LIST_INFORMATION[] (requires SeIncreaseQuotaPrivilege) // MmQuerySystemVaInformation
		SystemLogicalProcessorAndGroupInformation, // q: SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX // since WIN7 // KeQueryLogicalProcessorRelationship
		SystemProcessorCycleTimeInformation, // q: SYSTEM_PROCESSOR_CYCLE_TIME_INFORMATION[]
		SystemStoreInformation, // q; s: SYSTEM_STORE_INFORMATION // SmQueryStoreInformation
		SystemRegistryAppendString, // s: SYSTEM_REGISTRY_APPEND_STRING_PARAMETERS // 110
		SystemAitSamplingValue, // s: ULONG (requires SeProfileSingleProcessPrivilege)
		SystemVhdBootInformation, // q: SYSTEM_VHD_BOOT_INFORMATION
		SystemCpuQuotaInformation, // q; s // PsQueryCpuQuotaInformation
		SystemNativeBasicInformation, // not implemented
		SystemSpare1, // not implemented
		SystemLowPriorityIoInformation, // q: SYSTEM_LOW_PRIORITY_IO_INFORMATION
		SystemTpmBootEntropyInformation, // q: TPM_BOOT_ENTROPY_NT_RESULT // ExQueryTpmBootEntropyInformation
		SystemVerifierCountersInformation, // q: SYSTEM_VERIFIER_COUNTERS_INFORMATION
		SystemPagedPoolInformationEx, // q: SYSTEM_FILECACHE_INFORMATION; s (requires SeIncreaseQuotaPrivilege) (info for WorkingSetTypePagedPool)
		SystemSystemPtesInformationEx, // q: SYSTEM_FILECACHE_INFORMATION; s (requires SeIncreaseQuotaPrivilege) (info for WorkingSetTypeSystemPtes) // 120
		SystemNodeDistanceInformation,
		SystemAcpiAuditInformation, // q: SYSTEM_ACPI_AUDIT_INFORMATION // HaliQuerySystemInformation -> HalpAuditQueryResults, info class 26
		SystemBasicPerformanceInformation, // q: SYSTEM_BASIC_PERFORMANCE_INFORMATION // name:wow64:whNtQuerySystemInformation_SystemBasicPerformanceInformation
		SystemQueryPerformanceCounterInformation, // q: SYSTEM_QUERY_PERFORMANCE_COUNTER_INFORMATION // since WIN7 SP1
		SystemSessionBigPoolInformation, // q: SYSTEM_SESSION_POOLTAG_INFORMATION // since WIN8
		SystemBootGraphicsInformation, // q; s: SYSTEM_BOOT_GRAPHICS_INFORMATION (kernel-mode only)
		SystemScrubPhysicalMemoryInformation, // q; s: MEMORY_SCRUB_INFORMATION
		SystemBadPageInformation,
		SystemProcessorProfileControlArea, // q; s: SYSTEM_PROCESSOR_PROFILE_CONTROL_AREA
		SystemCombinePhysicalMemoryInformation, // s: MEMORY_COMBINE_INFORMATION, MEMORY_COMBINE_INFORMATION_EX, MEMORY_COMBINE_INFORMATION_EX2 // 130
		SystemEntropyInterruptTimingCallback,
		SystemConsoleInformation, // q: SYSTEM_CONSOLE_INFORMATION
		SystemPlatformBinaryInformation, // q: SYSTEM_PLATFORM_BINARY_INFORMATION
		SystemThrottleNotificationInformation,
		SystemHypervisorProcessorCountInformation, // q: SYSTEM_HYPERVISOR_PROCESSOR_COUNT_INFORMATION
		SystemDeviceDataInformation, // q: SYSTEM_DEVICE_DATA_INFORMATION
		SystemDeviceDataEnumerationInformation,
		SystemMemoryTopologyInformation, // q: SYSTEM_MEMORY_TOPOLOGY_INFORMATION
		SystemMemoryChannelInformation, // q: SYSTEM_MEMORY_CHANNEL_INFORMATION
		SystemBootLogoInformation, // q: SYSTEM_BOOT_LOGO_INFORMATION // 140
		SystemProcessorPerformanceInformationEx, // q: SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION_EX // since WINBLUE
		SystemSpare0,
		SystemSecureBootPolicyInformation, // q: SYSTEM_SECUREBOOT_POLICY_INFORMATION
		SystemPageFileInformationEx, // q: SYSTEM_PAGEFILE_INFORMATION_EX
		SystemSecureBootInformation, // q: SYSTEM_SECUREBOOT_INFORMATION
		SystemEntropyInterruptTimingRawInformation,
		SystemPortableWorkspaceEfiLauncherInformation, // q: SYSTEM_PORTABLE_WORKSPACE_EFI_LAUNCHER_INFORMATION
		SystemFullProcessInformation, // q: SYSTEM_PROCESS_INFORMATION with SYSTEM_PROCESS_INFORMATION_EXTENSION (requires admin)
		SystemKernelDebuggerInformationEx, // q: SYSTEM_KERNEL_DEBUGGER_INFORMATION_EX
		SystemBootMetadataInformation, // 150
		SystemSoftRebootInformation, // q: ULONG
		SystemElamCertificateInformation, // s: SYSTEM_ELAM_CERTIFICATE_INFORMATION
		SystemOfflineDumpConfigInformation,
		SystemProcessorFeaturesInformation, // q: SYSTEM_PROCESSOR_FEATURES_INFORMATION
		SystemRegistryReconciliationInformation,
		SystemEdidInformation,
		SystemManufacturingInformation, // q: SYSTEM_MANUFACTURING_INFORMATION // since THRESHOLD
		SystemEnergyEstimationConfigInformation, // q: SYSTEM_ENERGY_ESTIMATION_CONFIG_INFORMATION
		SystemHypervisorDetailInformation, // q: SYSTEM_HYPERVISOR_DETAIL_INFORMATION
		SystemProcessorCycleStatsInformation, // q: SYSTEM_PROCESSOR_CYCLE_STATS_INFORMATION // 160
		SystemVmGenerationCountInformation,
		SystemTrustedPlatformModuleInformation, // q: SYSTEM_TPM_INFORMATION
		SystemKernelDebuggerFlags, // SYSTEM_KERNEL_DEBUGGER_FLAGS
		SystemCodeIntegrityPolicyInformation, // q: SYSTEM_CODEINTEGRITYPOLICY_INFORMATION
		SystemIsolatedUserModeInformation, // q: SYSTEM_ISOLATED_USER_MODE_INFORMATION
		SystemHardwareSecurityTestInterfaceResultsInformation,
		SystemSingleModuleInformation, // q: SYSTEM_SINGLE_MODULE_INFORMATION
		SystemAllowedCpuSetsInformation,
		SystemVsmProtectionInformation, // q: SYSTEM_VSM_PROTECTION_INFORMATION (previously SystemDmaProtectionInformation)
		SystemInterruptCpuSetsInformation, // q: SYSTEM_INTERRUPT_CPU_SET_INFORMATION // 170
		SystemSecureBootPolicyFullInformation, // q: SYSTEM_SECUREBOOT_POLICY_FULL_INFORMATION
		SystemCodeIntegrityPolicyFullInformation,
		SystemAffinitizedInterruptProcessorInformation,
		SystemRootSiloInformation, // q: SYSTEM_ROOT_SILO_INFORMATION
		SystemCpuSetInformation, // q: SYSTEM_CPU_SET_INFORMATION // since THRESHOLD2
		SystemCpuSetTagInformation, // q: SYSTEM_CPU_SET_TAG_INFORMATION
		SystemWin32WerStartCallout,
		SystemSecureKernelProfileInformation, // q: SYSTEM_SECURE_KERNEL_HYPERGUARD_PROFILE_INFORMATION
		SystemCodeIntegrityPlatformManifestInformation, // q: SYSTEM_SECUREBOOT_PLATFORM_MANIFEST_INFORMATION // since REDSTONE
		SystemInterruptSteeringInformation, // 180
		SystemSupportedProcessorArchitectures,
		SystemMemoryUsageInformation, // q: SYSTEM_MEMORY_USAGE_INFORMATION
		SystemCodeIntegrityCertificateInformation, // q: SYSTEM_CODEINTEGRITY_CERTIFICATE_INFORMATION
		SystemPhysicalMemoryInformation, // q: SYSTEM_PHYSICAL_MEMORY_INFORMATION // since REDSTONE2
		SystemControlFlowTransition,
		SystemKernelDebuggingAllowed, // s: ULONG
		SystemActivityModerationExeState, // SYSTEM_ACTIVITY_MODERATION_EXE_STATE
		SystemActivityModerationUserSettings, // SYSTEM_ACTIVITY_MODERATION_USER_SETTINGS
		SystemCodeIntegrityPoliciesFullInformation,
		SystemCodeIntegrityUnlockInformation, // SYSTEM_CODEINTEGRITY_UNLOCK_INFORMATION // 190
		SystemIntegrityQuotaInformation,
		SystemFlushInformation, // q: SYSTEM_FLUSH_INFORMATION
		SystemProcessorIdleMaskInformation, // q: ULONG_PTR // since REDSTONE3
		SystemSecureDumpEncryptionInformation,
		SystemWriteConstraintInformation, // SYSTEM_WRITE_CONSTRAINT_INFORMATION
		SystemKernelVaShadowInformation, // SYSTEM_KERNEL_VA_SHADOW_INFORMATION
		SystemHypervisorSharedPageInformation, // SYSTEM_HYPERVISOR_SHARED_PAGE_INFORMATION // since REDSTONE4
		SystemFirmwareBootPerformanceInformation,
		SystemCodeIntegrityVerificationInformation, // SYSTEM_CODEINTEGRITYVERIFICATION_INFORMATION
		SystemFirmwarePartitionInformation, // SYSTEM_FIRMWARE_PARTITION_INFORMATION // 200
		SystemSpeculationControlInformation, // SYSTEM_SPECULATION_CONTROL_INFORMATION // (CVE-2017-5715) REDSTONE3 and above.
		SystemDmaGuardPolicyInformation, // SYSTEM_DMA_GUARD_POLICY_INFORMATION
		SystemEnclaveLaunchControlInformation, // SYSTEM_ENCLAVE_LAUNCH_CONTROL_INFORMATION
		SystemWorkloadAllowedCpuSetsInformation, // SYSTEM_WORKLOAD_ALLOWED_CPU_SET_INFORMATION // since REDSTONE5
		SystemCodeIntegrityUnlockModeInformation,
		SystemLeapSecondInformation, // SYSTEM_LEAP_SECOND_INFORMATION
		SystemFlags2Information, // q: SYSTEM_FLAGS_INFORMATION
		SystemSecurityModelInformation, // SYSTEM_SECURITY_MODEL_INFORMATION // since 19H1
		SystemCodeIntegritySyntheticCacheInformation,
		MaxSystemInfoClass
	} SYSTEM_INFORMATION_CLASS;
};

typedef LONG(WINAPI* PFN_NtQuerySystemInformation)(_SELF_::SYSTEM_INFORMATION_CLASS SystemInformationClass, PVOID SystemInformation,
	ULONG SystemInformationLength, PULONG ReturnLength);

class ProcessUtils_Win32
{
public:

	inline bool EnablePrivilege(const wchar_t* privilegeName, bool bEnable);

	inline bool getProcessHandleByPid(int64_t processId, HANDLE& processHndle);

	inline bool getProcessCmdLineByPid(uint32_t dwPID, std::wstring& strCmdLine, uint32_t* errCode = nullptr);
	inline bool getProcessCmdLineByHandle(HANDLE hProcess, std::wstring& strCmdLine, uint32_t* errCode = nullptr);

	inline bool getProcessCurDirByPid(uint32_t dwPID, std::wstring& strCurDir, uint32_t* errCode = nullptr);
	inline bool getProcessCurDirByHandle(HANDLE hProcess, std::wstring& strCurDir, uint32_t* errCode = nullptr);

	inline bool getProcessThreadCount(uint32_t dwPID, uint32_t& threadCount);
	inline bool getProcessIntegrityLevel(HANDLE hProcess, std::string& integrityLevel);

	inline bool getProcessFullPathByPid(uint32_t dwPID, std::wstring& processFullPath);
	inline bool getProcessFullPathByHandle(HANDLE hProcess, std::wstring& processFullPath);

	inline bool getProcessTimesByHandle(HANDLE hProcess, int64_t& creationDate, int64_t& terminationDate, int64_t& kernelModeTime,
		int64_t& userModeTime);

	inline bool getProcessUserAndSessionInfosByHandle(HANDLE hProcess, std::wstring& userName, std::wstring& userDomainName,
		uint32_t& processSessionId, std::wstring& logonSid);
	inline bool getProcessParentPidByPid( int64_t processId , int64_t& parentpid );

public:

	static inline ProcessUtils_Win32* getInstancePtr()
	{
		std::lock_guard<std::mutex> lck(m_instanceLck);
		static ProcessUtils_Win32* _instance = nullptr;
		if(nullptr == _instance)
		{
			_instance = new ProcessUtils_Win32();
			_instance->_init();
		}
		return _instance;
	}

protected:

	inline ProcessUtils_Win32();

protected:

	inline bool _init();

	inline bool _DosPathToNtPath(wchar_t* pszDosPath, wchar_t* pszNtPath);

	inline bool _getProcessCmdLineByHandleInMemory(HANDLE hProcess, std::wstring& strCmdLine, uint32_t* errCode = nullptr);
	
	inline bool _getProcessCmdLineByHandleInMemory_x64(HANDLE hProcess, std::wstring& strCmdLine, uint32_t* errCode = nullptr);

	inline bool _getProcessCurDirByHandleInMemory(HANDLE hProcess, std::wstring& strCurDir, uint32_t* errCode = nullptr);

	inline bool _getProcessCurDirByHandleInMemory_x64(HANDLE hProcess, std::wstring& strCurDir, uint32_t* errCode = nullptr);

	inline NTSTATUS _queryProcessVariableSize(HANDLE ProcessHandle, _SELF_::PROCESSINFOCLASS_SELF ProcessInformationClass, PVOID* Buffer);

private:

	PFN_NtQuerySystemInformation			m_NtQuerySystemInformation;
	PFN_NtQueryInformationProcess			m_NtQueryInformationProcess;
	PFN_NtWow64QueryInformationProcess64	m_NtWow64QueryInformationProcess64;
	
	PFN_IsWow64Process						m_pfn_IsWow64Process;
	PFN_NtReadVirtualMemory					m_NtReadVirtualMemory;
	PFN_RtlNtStatusToDosError				m_RtlNtStatusToDosError;
	PFN_NtWow64ReadVirtualMemory64			m_NtWow64ReadVirtualMemory64;

	std::mutex m_initLck;
	static std::mutex m_instanceLck;

	bool m_inited;
	bool m_bCurrent64BitSystem;

	const uint32_t SYSTEM_PROCESS_PID = 4;
	const wchar_t* SYSTEM_PROCESS_CMDLINE = L"system cmdline N/A";
};

typedef struct
{
	DWORD MaximumLength;
	DWORD Length;
	DWORD Flags;
	DWORD DebugFlags;
	PVOID ConsoleHandle;
	DWORD ConsoleFlags;
	PVOID StandardInput;
	PVOID StandardOutput;
	PVOID StandardError;
	//////////////////////////
	UNICODE_STRING DosPath;    //CurrentDirectory
	HANDLE Handle;
	//////////////////////////
	UNICODE_STRING DllPath;
	UNICODE_STRING ImagePathName;
	UNICODE_STRING CmdLine;
	//бнбн
}__RTL_USER_PROCESS_PARAMETERS;

typedef struct
{
	DWORD MaximumLength;
	DWORD Length;
	DWORD Flags;
	DWORD DebugFlags;
	PVOID64 ConsoleHandle;
	DWORD ConsoleFlags;
	PVOID64 StandardInput;
	PVOID64 StandardOutput;
	PVOID64 StandardError;
	//////////////////////////
	UNICODE_STRING64 DosPath;//CurrentDirectory
	HANDLE Handle;
	//////////////////////////
	UNICODE_STRING64 DllPath;
	UNICODE_STRING64 ImagePathName;
	UNICODE_STRING64 CmdLine;
	//бнбн
}__RTL_USER_PROCESS_PARAMETERS64;

#include "ProcessUtil_win32.inl"

#endif // ProcessUtil_win32_h