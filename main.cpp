#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include <pthread.h>
#include <unistd.h>

#include <windows.h>
#include <psapi.h>
#include <memoryapi.h>
#include <heapapi.h>

#include <dinput.h>

FILE *log_file = NULL;
#define LOG(...){ \
	if(log_file == NULL){ \
		log_file = fopen("./project_torque_ffb_hotpatch_log.txt", "w"); \
	} \
	if(log_file != NULL){ \
		fprintf(log_file, __VA_ARGS__); \
		fflush(log_file); \
	} \
}

static void log_buffer(uint8_t *buf, size_t size){
	for(size_t i = 0;i < size;i++){
		LOG("0x%02x ", buf[i]);
	}
}

void patch(uint32_t location, uint8_t *buf, size_t size){
	DWORD old_protect;
	VirtualProtect((void *)location, size, PAGE_EXECUTE_READWRITE, &old_protect);
	LOG("0x%08x: ", location);
	log_buffer((uint8_t *)location, size);
	memcpy((void *)location, buf, size);
	LOG("-> ");
	log_buffer((uint8_t *)location, size);
	LOG("\n");
	VirtualProtect((void *)location, size, old_protect, &old_protect);
}

void find_and_patch(void *begin, size_t size, uint8_t *target, uint8_t *patch_buf, size_t patch_size, uint32_t first_time, uint32_t times){
	uint32_t i = 0;
	for(uint32_t offset = 0;offset <= size - patch_size;offset++){
		if(memcmp((void *)(offset + (uint32_t)begin), target, patch_size) == 0){
			if(i >= first_time && i < times + first_time){
				patch(offset + (uint32_t)begin, patch_buf, patch_size);
			}
			if(i >= times + first_time){
				break;
			}
			i++;
		}
	}
}

// IDirectInputEffect::SetParameters
static uint32_t set_parameters(uint32_t effect_object, DIEFFECT *effect, uint32_t flags){
	uint32_t (__attribute__((stdcall)) *f)(uint32_t, uint32_t, uint32_t) = (uint32_t (__attribute__((stdcall)) *)(uint32_t, uint32_t, uint32_t))*(uint32_t *)(*(uint32_t *)effect_object + 0x18);
	return f(effect_object, (uint32_t)effect, flags);
}

static int32_t clamp_int32(int32_t min, int32_t max, int32_t value){
	if(value > max){
		return max;
	}
	if(value < min){
		return min;
	}
	return value;
}

void adjust_spring_effect(void *begin){
	uint32_t zero = 0;

	// spring
	patch((uint32_t)begin + 0x199a5d + 6, (uint8_t *)&zero, sizeof(zero));
	patch((uint32_t)begin + 0x199a67 + 3, (uint8_t *)&zero, sizeof(zero));
}


static uint32_t init_effects_offset = 0;
static uint32_t (__attribute__((thiscall))*init_effects_orig)(uint32_t param_1, uint32_t param_2);
uint32_t __attribute__((thiscall)) init_effects_patched(uint32_t param_1, uint32_t param_2){
	float max_vibration_setting = *(float *)(init_effects_offset + 0x70e584);

	LOG("%s: setting damper to %f\n", __func__, max_vibration_setting);

	// damper
	uint32_t sat = clamp_int32(0, 10000, (int32_t)max_vibration_setting);
	uint32_t coeff = sat;
	patch(init_effects_offset + 0x199a7c + 3, (uint8_t *)&coeff, sizeof(coeff));
	patch(init_effects_offset + 0x199a83 + 3, (uint8_t *)&coeff, sizeof(coeff));
	patch(init_effects_offset + 0x199a8a + 3, (uint8_t *)&sat, sizeof(sat));
	patch(init_effects_offset + 0x199a91 + 3, (uint8_t *)&sat, sizeof(sat));

	return init_effects_orig(param_1, param_2);
}

void hook_inif_effects(void* begin){
	static bool once = 0;
	if(once){
		return;
	}
	once = true;

	const uint32_t target_offset = (uint32_t)begin + 0x199a00;
	init_effects_offset = (uint32_t)begin;

	static uint8_t trampoline[] = {
		// original instruction
		0x00,
		0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00,

		// mov eax, val
		0xb8, 0x00, 0x00, 0x00, 0x00,
		// jmp eax
		0xff, 0xe0
	};
	memcpy(trampoline, (void *)target_offset, 9);
	*(uint32_t *)&trampoline[10] = target_offset + 9;

	init_effects_orig = (uint32_t (__attribute__((thiscall))*)(uint32_t param_1, uint32_t param_2))trampoline;
	DWORD old_protect;
	int ret = VirtualProtect(trampoline, sizeof(trampoline), PAGE_EXECUTE_READWRITE, &old_protect);
	if(ret == 0){
		LOG("%s: failed setting virtual protect for trampoline, %x\n", __func__, GetLastError());
		return;
	}

	uint8_t patch_buf[] = {
		// mov eax, val
		0xb8, 0x00, 0x00, 0x00, 0x00,

		// jmp eax
		0xff, 0xe0
	};
	*(uint32_t *)&patch_buf[1] = (uint32_t)init_effects_patched;

	patch(target_offset, patch_buf, sizeof(patch_buf));
}


static uint32_t send_constant_force_offset = 0;
static uint32_t (__attribute__((stdcall)) *send_constant_force_orig)(void *ctx, float param_1, float param_2);
uint32_t __attribute__((stdcall))send_constant_force_patched(void *ctx, float param_1, float param_2){
	#if 0
	static float param_1_max = 0;
	float param_1_abs = abs(param_1);
	if(param_1_abs > param_1_max){
		param_1_max = param_1_abs;
	}

	LOG("%s: 0x%08x %f %f %f\n", __func__, ctx, param_1, param_2, param_1_max);
	#endif

	// boost
	#if 1
		param_1 = param_1 * 10;
	#endif

	// boost low end
	#if 0
	if(param_1 < 0.1){
		param_1 = param_1 * 0.5 / 0.1;
	}else{
		param_1 = 0.5 + (param_1 - 0.1) * 0.5 / 0.9;
	}
	#endif

	uint32_t unknown_object = *(uint32_t *)((uint32_t)ctx + 0x1408);
	if(unknown_object == 0){
		//LOG("%s: unknown object is NULL\n", __func__);
		return 0;
	}

	// just send param_1, param_2 seems to be 0 all the time
	uint32_t effect_object = *(uint32_t *) (unknown_object + 0x14);
	if(effect_object == 0){
		//LOG("%s: effect object is NULL\n", __func__);
		return 0;
	}

	uint32_t ndirections = *(uint32_t *)((uint32_t)ctx + 0x1404);
	float max_force_setting = *(float *)(send_constant_force_offset + 0x70e580);
	DIEFFECT effect = {0};
	DICONSTANTFORCE constant_force = {.lMagnitude = clamp_int32(-10000, 10000, (int32_t)(param_1 * max_force_setting))};
	LONG directions[ndirections] = {0};
	#ifndef REVERSED
	directions[0] = 1;
	#else
	directions[0] = -1;
	#endif
	effect.dwSize = sizeof(effect);
	effect.dwFlags = DIEFF_OBJECTOFFSETS | DIEFF_CARTESIAN;
	effect.cAxes = ndirections;
	effect.rglDirection = directions;
	effect.cbTypeSpecificParams = sizeof(constant_force);
	effect.lpvTypeSpecificParams = &constant_force;

	uint32_t ret = set_parameters(effect_object, &effect, DIEP_TYPESPECIFICPARAMS | DIEP_DIRECTION);
	//LOG("SetParameters ret %x\n", ret);
	return ret;

	//return send_constant_force_orig(ctx, param_1, param_2);
}

void hook_send_constant_force(void* begin){
	static bool once = 0;
	if(once){
		return;
	}
	once = true;

	#if 1
	// remove limiter
	uint32_t target = (uint32_t)begin + 0x198851 + 9;
	send_constant_force_offset = (uint32_t)begin;
	uint8_t patch_byte = 0xeb;
	patch(target, &patch_byte, sizeof(patch_byte));

	// disable other effects
	target = (uint32_t)begin + 0x19DA8F;
	patch(target, &patch_byte, sizeof(patch_byte));

	// patch ffb filter
	uint8_t patch_ffb_filter[] = {0x90, 0x90};
	target = (uint32_t)begin + 0x19cf24;
	patch(target, patch_ffb_filter, sizeof(patch_ffb_filter));
	#endif

	const uint32_t target_offset = (uint32_t)begin + 0x1987C0;

	static uint8_t trampoline[] = {
		// original instruction
		0x00,
		0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00,

		// mov eax, val
		0xb8, 0x00, 0x00, 0x00, 0x00,
		// jmp eax
		0xff, 0xe0
	};
	memcpy(trampoline, (void *)target_offset, 9);
	*(uint32_t *)&trampoline[10] = target_offset + 9;

	send_constant_force_orig = (uint32_t (__attribute__((stdcall)) *)(void *ctx, float param_1, float param_2))trampoline;
	DWORD old_protect;
	int ret = VirtualProtect(trampoline, sizeof(trampoline), PAGE_EXECUTE_READWRITE, &old_protect);
	if(ret == 0){
		LOG("%s: failed setting virtual protect for trampoline, %x\n", __func__, GetLastError());
		return;
	}

	uint8_t patch_buf[] = {
		// mov eax, val
		0xb8, 0x00, 0x00, 0x00, 0x00,

		// jmp eax
		0xff, 0xe0
	};
	*(uint32_t *)&patch_buf[1] = (uint32_t)send_constant_force_patched;

	patch(target_offset, patch_buf, sizeof(patch_buf));
}

static void patch(void *begin, size_t size){
	// hook
	hook_send_constant_force(begin);
	adjust_spring_effect(begin);
	hook_inif_effects(begin);
}

void *patch_thread(void *arg){
	LOG("%s: begins\n", __func__);

	while(true){
		sleep(0);

		LOG("enumerating modules:\n");

		HMODULE modules[1024];
		DWORD num_modules;
		int ret = EnumProcessModulesEx(GetCurrentProcess(), modules, sizeof(modules), &num_modules, LIST_MODULES_ALL);
		if(ret == 0){
			LOG("failed enumerating modules, %x\n", GetLastError());
			continue;
		}
		for(int i = 0;i<num_modules/sizeof(HMODULE);i++){
			char module_name[1024] = {0};
			ret = GetModuleBaseNameA(GetCurrentProcess(), modules[i], module_name, sizeof(module_name));
			if(ret == 0){
				LOG("faield fetching module name %d, %x\n", i, GetLastError());
				continue;
			}
			MODULEINFO info;
			ret = GetModuleInformation(GetCurrentProcess(), modules[i], &info, sizeof(info));
			if(ret == 0){
				LOG("failed fetching module information %d, %x\n", i, GetLastError());
				continue;
			}
			LOG("module %s at 0x%08x\n", module_name, info.lpBaseOfDll);

			if(strcmp("CRC_ReleaseNoDebug.dll", module_name) == 0){
				LOG("target found\n");

				patch(info.lpBaseOfDll, info.SizeOfImage);
				// what the hell is this
				//patch((void *)0x0BE10000, info.SizeOfImage);
				return NULL;
			}
		}

	}

	return NULL;
}

__attribute__((constructor))
int patch(){
	LOG("%s: begins\n", __func__);

	pthread_t t;
	pthread_create(&t, NULL, patch_thread, NULL);
	pthread_detach(t);

	return 0;
}
