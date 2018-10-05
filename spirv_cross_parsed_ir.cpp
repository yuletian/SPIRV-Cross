/*
 * Copyright 2018 Arm Limited
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "spirv_cross_parsed_ir.hpp"
#include <assert.h>

using namespace std;
using namespace spv;

namespace spirv_cross
{
void ParsedIR::set_id_bounds(uint32_t bounds)
{
	ids.resize(bounds);
	meta.resize(bounds);
}

static string ensure_valid_identifier(const string &name, bool member)
{
	// Functions in glslangValidator are mangled with name(<mangled> stuff.
	// Normally, we would never see '(' in any legal identifiers, so just strip them out.
	auto str = name.substr(0, name.find('('));

	for (uint32_t i = 0; i < str.size(); i++)
	{
		auto &c = str[i];

		if (member)
		{
			// _m<num> variables are reserved by the internal implementation,
			// otherwise, make sure the name is a valid identifier.
			if (i == 0)
				c = isalpha(c) ? c : '_';
			else if (i == 2 && str[0] == '_' && str[1] == 'm')
				c = isalpha(c) ? c : '_';
			else
				c = isalnum(c) ? c : '_';
		}
		else
		{
			// _<num> variables are reserved by the internal implementation,
			// otherwise, make sure the name is a valid identifier.
			if (i == 0 || (str[0] == '_' && i == 1))
				c = isalpha(c) ? c : '_';
			else
				c = isalnum(c) ? c : '_';
		}
	}
	return str;
}

const string &ParsedIR::get_name(uint32_t id) const
{
	return meta[id].decoration.alias;
}

void ParsedIR::set_name(uint32_t id, const string &name)
{
	auto &str = meta[id].decoration.alias;
	str.clear();

	if (name.empty())
		return;

	// glslang uses identifiers to pass along meaningful information
	// about HLSL reflection.
	// FIXME: This should be deprecated eventually.
	auto &m = meta[id];
	if (source.hlsl && name.size() >= 6 && name.find("@count") == name.size() - 6)
	{
		m.hlsl_magic_counter_buffer_candidate = true;
		m.hlsl_magic_counter_buffer_name = name.substr(0, name.find("@count"));
	}
	else
	{
		m.hlsl_magic_counter_buffer_candidate = false;
		m.hlsl_magic_counter_buffer_name.clear();
	}

	// Reserved for temporaries.
	if (name[0] == '_' && name.size() >= 2 && isdigit(name[1]))
		return;

	str = ensure_valid_identifier(name, false);
}

void ParsedIR::set_member_name(uint32_t id, uint32_t index, const std::string &name)
{
	meta[id].members.resize(max(meta[id].members.size(), size_t(index) + 1));

	auto &str = meta[id].members[index].alias;
	str.clear();
	if (name.empty())
		return;

	// Reserved for unnamed members.
	if (name[0] == '_' && name.size() >= 3 && name[1] == 'm' && isdigit(name[2]))
		return;

	str = ensure_valid_identifier(name, true);
}

void ParsedIR::set_decoration_string(uint32_t id, Decoration decoration, const std::string &argument)
{
	auto &dec = meta[id].decoration;
	dec.decoration_flags.set(decoration);

	switch (decoration)
	{
	case DecorationHlslSemanticGOOGLE:
		dec.hlsl_semantic = argument;
		break;

	default:
		break;
	}
}

void ParsedIR::set_decoration(uint32_t id, Decoration decoration, uint32_t argument)
{
	auto &dec = meta[id].decoration;
	dec.decoration_flags.set(decoration);

	switch (decoration)
	{
	case DecorationBuiltIn:
		dec.builtin = true;
		dec.builtin_type = static_cast<BuiltIn>(argument);
		break;

	case DecorationLocation:
		dec.location = argument;
		break;

	case DecorationComponent:
		dec.component = argument;
		break;

	case DecorationOffset:
		dec.offset = argument;
		break;

	case DecorationArrayStride:
		dec.array_stride = argument;
		break;

	case DecorationMatrixStride:
		dec.matrix_stride = argument;
		break;

	case DecorationBinding:
		dec.binding = argument;
		break;

	case DecorationDescriptorSet:
		dec.set = argument;
		break;

	case DecorationInputAttachmentIndex:
		dec.input_attachment = argument;
		break;

	case DecorationSpecId:
		dec.spec_id = argument;
		break;

	case DecorationIndex:
		dec.index = argument;
		break;

	case DecorationHlslCounterBufferGOOGLE:
		meta[id].hlsl_magic_counter_buffer = argument;
		meta[id].hlsl_is_magic_counter_buffer = true;
		break;

	default:
		break;
	}
}

void ParsedIR::set_member_decoration(uint32_t id, uint32_t index, Decoration decoration, uint32_t argument)
{
	meta[id].members.resize(max(meta[id].members.size(), size_t(index) + 1));
	auto &dec = meta[id].members[index];
	dec.decoration_flags.set(decoration);

	switch (decoration)
	{
	case DecorationBuiltIn:
		dec.builtin = true;
		dec.builtin_type = static_cast<BuiltIn>(argument);
		break;

	case DecorationLocation:
		dec.location = argument;
		break;

	case DecorationComponent:
		dec.component = argument;
		break;

	case DecorationBinding:
		dec.binding = argument;
		break;

	case DecorationOffset:
		dec.offset = argument;
		break;

	case DecorationSpecId:
		dec.spec_id = argument;
		break;

	case DecorationMatrixStride:
		dec.matrix_stride = argument;
		break;

	case DecorationIndex:
		dec.index = argument;
		break;

	default:
		break;
	}
}

// Recursively marks any constants referenced by the specified constant instruction as being used
// as an array length. The id must be a constant instruction (SPIRConstant or SPIRConstantOp).
void ParsedIR::mark_used_as_array_length(uint32_t id)
{
	switch (ids[id].get_type())
	{
	case TypeConstant:
		get<SPIRConstant>(id).is_used_as_array_length = true;
		break;

	case TypeConstantOp:
	{
		auto &cop = get<SPIRConstantOp>(id);
		for (uint32_t arg_id : cop.arguments)
			mark_used_as_array_length(arg_id);
		break;
	}

	case TypeUndef:
		break;

	default:
		assert(0);
	}
}

}