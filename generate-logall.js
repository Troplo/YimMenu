const fs = require('fs');
const path = require('path');

const inputFile = path.join(__dirname, './src/natives.hpp');
const outputHeaderFile = path.join(__dirname, './src/native_hooks/log_all.hpp');
const outputCppFile = path.join(__dirname, './src/native_hooks/log_all.cpp');

const fileContent = fs.readFileSync(inputFile, 'utf-8');

const enumRegex = /enum class NativeIndex\s*{([\s\S]*?)}/;
const enumMatch = fileContent.match(enumRegex);
if (!enumMatch) {
    console.error('NativeIndex enum not found!');
    process.exit(1);
}

const enumBody = enumMatch[1];
const enumEntries = {};
enumBody.split(',')
    .map(line => line.trim())
    .filter(line => line.length > 0)
    .forEach(line => {
        const [name, value] = line.split('=').map(s => s.trim());
        if (value !== undefined) {
            enumEntries[parseInt(value, 10)] = name;
        }
    });

const lines = fileContent.split(/\r?\n/);
let currentNamespace = null;

let headerLines = [];
headerLines.push(`#pragma once
#include "native_hooks.hpp"

namespace lua { namespace native {
`);

let cppLines = [];
cppLines.push(`#include "log_all.hpp"
#include "logger/logger.hpp"
#include "natives.hpp"

namespace lua { namespace native {
`);

let detourLines = [];

const funcRegex = /FORCEINLINE\s+constexpr\s+(\w[\w:<>, ]*)\s+(\w+)\s*\(([^)]*)\)\s*{[^}]*invoke<\s*(\d+)\s*,/;

const blacklisted = [
    // causes exception
    "STAT_SAVE_MIGRATION_CONSUME_CONTENT",
    "STAT_MIGRATE_SAVEGAME_START",
    "STAT_MIGRATE_SAVEGAME_GET_STATUS",
    "STAT_MIGRATE_CHECK_START",
    "STAT_GET_SAVE_MIGRATION_STATUS",
    "SET_MISSION_NAME",
    "GET_BOSS_GOON_UUID",
    "SC_EMAIL_SET_CURRENT_EMAIL_TAG",
    "SET_PED_STEALTH_MOVEMENT",
    "NETWORK_REGISTER_HOST_BROADCAST_VARIABLES",
    "NETWORK_REGISTER_PLAYER_BROADCAST_VARIABLES",
    "NETWORK_REGISTER_HIGH_FREQUENCY_HOST_BROADCAST_VARIABLES",
    "NETWORK_REGISTER_ENTITY_AS_NETWORKED",
    // unimportant
    "HIDE_HUD_COMPONENT_THIS_FRAME",
    "TO_FLOAT",
    "NETWORK_IS_GAME_IN_PROGRESS",
    "WAIT",
    "SIN",
    "COS",
    "TAN",
    "GET_OFFSET_FROM_COORD_AND_HEADING_IN_WORLD_COORDS",
    "GET_BASE_ELEMENT_LOCATION_FROM_METADATA_BLOCK",
    "START_NEW_SCRIPT",
    "START_NEW_SCRIPT_WITH_ARGS",
    "START_NEW_SCRIPT_WITH_NAME_HASH",
    "START_NEW_SCRIPT_WITH_NAME_HASH_AND_ARGS",
    "IS_OBJECT_WITHIN_BRAIN_ACTIVATION_RANGE",
    "HAS_OBJECT_BEEN_BROKEN",
    "DOES_ENTITY_EXIST",
    "GET_ENTITY_MODEL",
    "PLAYER_PED_ID",
    "IS_PED_INJURED",
    "GET_DISTANCE_BETWEEN_COORDS",
    "GET_ENTITY_COORDS",
    "IS_CUTSCENE_PLAYING",
    "IS_WORLD_POINT_WITHIN_BRAIN_ACTIVATION_RANGE",
    "HIDE_HUD_AND_RADAR_THIS_FRAME",
    "HIDE_HELP_TEXT_THIS_FRAME",
    "NETWORK_CAN_ACCESS_MULTIPLAYER",
    "VEHICLE_SET_OVERRIDE_SIDE_RATIO",
    "CLEAR_BIT",
    "SET_BIT",
    "NETWORK_PLAYER_ID_TO_INT",
    "NETWORK_IS_PLAYER_ACTIVE",
    "IS_ENTITY_DEAD",
    "STAT_MIGRATE_CHECK_ALREADY_DONE",
    "NETWORK_IS_TUNABLE_CLOUD_REQUEST_PENDING",
    "NETWORK_IS_CLOUD_BACKGROUND_SCRIPT_REQUEST_PENDING",
    "IS_SOCIAL_CLUB_ACTIVE",
    "ARE_ONLINE_POLICIES_UP_TO_DATE",
    "GET_PLAYER_INDEX",
    "IS_PLAYER_PLAYING",
    "NETWORK_DOES_NETWORK_ID_EXIST",
    "IS_PLAYER_CONTROL_ON",
    "IS_ENTITY_PLAYING_ANIM",
    "GET_FRAME_COUNT",
    "GET_HUD_COLOUR",
    "DOES_BLIP_EXIST",
    "SET_BLIP_SCALE",
    "GET_BLIP_COORDS",
    "DRAW_MARKER",
    "ABSI",
    "ABS",
    "NETWORK_IS_ACTIVITY_SESSION",
    "IS_PLAYER_IN_CUTSCENE",
    "IS_USING_KEYBOARD_AND_MOUSE",
    "IS_PED_FALLING",
    "IS_PED_GETTING_INTO_A_VEHICLE",
    "GET_PED_PARACHUTE_STATE",
    "NETWORK_IS_IN_MP_CUTSCENE",
    "NETWORK_IS_PLAYER_TALKING",
    "GET_LODSCALE",
    "GET_PLAYER_WANTED_LEVEL",
    "GET_PLAYER_FAKE_WANTED_LEVEL",
    "VDIST2",
    "VDIST",
    "VMAG2",
    "VMAG",
    "IS_PLAYER_WANTED_LEVEL_GREATER",
    "IS_PED_IN_ANY_VEHICLE",
    "GET_TIME_DIFFERENCE",
    "FLOOR",
    "CEIL",
    "ROUND",
    "SHIFT_LEFT",
    "SHIFT_RIGHT"
]

lines.forEach(line => {
    line = line.trim();

    const nsMatch = line.match(/^namespace\s+(\w+)/);
    if (nsMatch) currentNamespace = nsMatch[1];

    const funcMatch = line.match(funcRegex);
    if (funcMatch && currentNamespace) {
        const returnType = funcMatch[1];
        const funcName = funcMatch[2];
        const argsString = funcMatch[3];
        const index = parseInt(funcMatch[4], 10);
        const enumName = enumEntries[index];
        if (!enumName) return;
        if(blacklisted.includes(enumName)) return;
        if(enumName.startsWith("NETWORK_")) return;

        const luaNativeName = `LUA_NATIVE_${currentNamespace.toUpperCase()}_${enumName.toUpperCase()}`;
        const wrapperName = `${luaNativeName}_LOG`;
        const args = argsString.length ? argsString.split(',').map(a => a.trim()) : [];
        const argDetails = args.map((arg) => {
            const match = arg.match(/^(.*\S)\s+(\w+)$/);
            if (match) {
                return {type: match[1], name: match[2]};
            }
            return {type: 'Any', name: 'arg'};
        });
        const argList = argDetails.map((a, i) => `src->get_arg<${a.type}>(${i})`);
        headerLines.push(`void ${wrapperName}(rage::scrNativeCallContext* src);`);

        cppLines.push(`void ${wrapperName}(rage::scrNativeCallContext* src) {`);

        if(1) {
            // Single-line log with function name and all arguments
            cppLines.push(`    LOG(VERBOSE) << "${funcName}"` +
                (argDetails.length
                    ? ' << " | " << ' + argDetails.map((a, i) => `"${a.name}: " << src->get_arg<${a.type}>(${i})`).join(' << ", " << ')
                    : '') + ';');
        } else {
            cppLines.push(`    LOG(VERBOSE) << "${funcName}";`);
        }

        if (returnType === 'void') {
            cppLines.push(`    ${currentNamespace}::${funcName}(${argList.join(', ')});`);
        } else {
            cppLines.push(`    src->set_return_value<${returnType}>(${currentNamespace}::${funcName}(${argList.join(', ')}));`);
        }
        cppLines.push('}\n');

        detourLines.push(`    nhooks->add_native_detour(NativeIndex::${enumName}, lua::native::${luaNativeName}_LOG);`);
    }
});

headerLines.push('}\n}\n');
headerLines.push('namespace big {');
headerLines.push('void init_native_hooks_logs(native_hooks* nhooks);');
headerLines.push('}');

cppLines.push('}\n}\n');
cppLines.push('namespace big {');
cppLines.push('void init_native_hooks_logs(native_hooks* nhooks) {');
cppLines = cppLines.concat(detourLines);
cppLines.push('}\n}');

fs.writeFileSync(outputHeaderFile, headerLines.join('\n'));
fs.writeFileSync(outputCppFile, cppLines.join('\n'));
console.log(`Generated ${outputHeaderFile} and ${outputCppFile}`);