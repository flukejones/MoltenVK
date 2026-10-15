/*
 * SPIRVReflection.h
 *
 * Copyright (c) 2019-2026 Chip Davis for Codeweavers
 * Modified for Headgasket VR, 2026.
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

#ifndef __SPIRVReflection_h_
#define __SPIRVReflection_h_ 1

#include <spirv.hpp>
#include <spirv_common.hpp>
#include <spirv_cross.hpp>
#include <spirv_parser.hpp>
#include <spirv_reflect.hpp>
#include <algorithm>
#include <string>
#include <utility>
#include <vector>


namespace mvk {

#pragma mark -
#pragma mark SPIRVTessReflectionData

	/**
	 * Reflection data for a pair of tessellation shaders.
	 * This contains the information needed to construct a tessellation pipeline.
	 */
	struct SPIRVTessReflectionData {
		/** The partition mode, one of SpacingEqual, SpacingFractionalEven, or SpacingFractionalOdd. */
		spv::ExecutionMode partitionMode = spv::ExecutionModeMax;

		/** The winding order of generated triangles, one of VertexOrderCw or VertexOrderCcw. */
		spv::ExecutionMode windingOrder = spv::ExecutionModeMax;

		/** Whether or not tessellation should produce points instead of lines or triangles. */
		bool pointMode = false;

		/** The kind of patch expected as input, one of Triangles, Quads, or Isolines. */
		spv::ExecutionMode patchKind = spv::ExecutionModeMax;

		/** The number of control points output by the tessellation control shader. */
		uint32_t numControlPoints = 0;
	};

	/** Low execution-mode flags used by tessellation; outputVertices is valid only with OutputVertices. */
	struct SPIRVTessExecutionModes {
		SPIRV_CROSS_NAMESPACE::Bitset modes;
		uint32_t outputVertices = 0;
	};

#pragma mark -
#pragma mark SPIRVShaderInterfaceVariable

	/**
	 * Reflection data on a single interface variable of a shader.
	 * This contains the information needed to construct a
	 * stage-input descriptor for the next stage of a pipeline.
	 */
	struct SPIRVShaderInterfaceVariable {
		/** The type of the variable. */
		SPIRV_CROSS_NAMESPACE::SPIRType::BaseType baseType;

		/** The vector size, if a vector. */
		uint32_t vecWidth;

		/** The location number of the variable. */
		uint32_t location;

		/** The component index of the variable. */
		uint32_t component;

		/**
		 * If this is the first member of a struct, this will contain the alignment
		 * of the struct containing this variable, otherwise this will be zero.
		 */
		uint32_t firstStructMemberAlignment;

		/** If this is a builtin, the kind of builtin this is. */
		spv::BuiltIn builtin;

		/** Whether this is a per-patch or per-vertex variable. Only meaningful for tessellation shaders. */
		bool perPatch;

		/** Whether this variable is actually used (read or written) by the shader. */
		bool isUsed;
	};
	typedef SPIRVShaderInterfaceVariable SPIRVShaderOutput;


#pragma mark -
#pragma mark Functions

	/** Extracts literal modes for one entry point; parser or entry-point errors leave metadata unchanged. */
	template<typename Vs>
	static inline bool getTessExecutionModes(const Vs& spirv, spv::ExecutionModel model,
										const std::string& entryName, SPIRVTessExecutionModes& metadata,
										std::string& errorLog) {
#ifndef SPIRV_CROSS_EXCEPTIONS_TO_ASSERTIONS
		try {
#endif
			SPIRV_CROSS_NAMESPACE::Compiler reflect(spirv);
			if (!entryName.empty()) { reflect.set_entry_point(entryName, model); }
			SPIRVTessExecutionModes reflected;
			reflected.modes = SPIRV_CROSS_NAMESPACE::Bitset(reflect.get_execution_mode_bitset().get_lower());
			if (reflected.modes.get(spv::ExecutionModeOutputVertices)) {
				reflected.outputVertices = reflect.get_execution_mode_argument(spv::ExecutionModeOutputVertices);
			}
			metadata = std::move(reflected);
			return true;
#ifndef SPIRV_CROSS_EXCEPTIONS_TO_ASSERTIONS
		} catch (SPIRV_CROSS_NAMESPACE::CompilerError& ex) {
			errorLog = ex.what();
			return false;
		}
#endif
	}

	/** Merges tessellation modes, preferring control-stage values over evaluation-stage values. */
	static inline bool getTessReflectionData(const SPIRVTessExecutionModes& tesc,
											const SPIRVTessExecutionModes& tese,
											SPIRVTessReflectionData& reflectData, std::string& errorLog) {
		const SPIRV_CROSS_NAMESPACE::Bitset& tescModes = tesc.modes;
		const SPIRV_CROSS_NAMESPACE::Bitset& teseModes = tese.modes;
		if (tescModes.get(spv::ExecutionModeTriangles)) {
			reflectData.patchKind = spv::ExecutionModeTriangles;
		} else if (tescModes.get(spv::ExecutionModeQuads)) {
			reflectData.patchKind = spv::ExecutionModeQuads;
		} else if (tescModes.get(spv::ExecutionModeIsolines)) {
			reflectData.patchKind = spv::ExecutionModeIsolines;
		} else if (teseModes.get(spv::ExecutionModeTriangles)) {
			reflectData.patchKind = spv::ExecutionModeTriangles;
		} else if (teseModes.get(spv::ExecutionModeQuads)) {
			reflectData.patchKind = spv::ExecutionModeQuads;
		} else if (teseModes.get(spv::ExecutionModeIsolines)) {
			reflectData.patchKind = spv::ExecutionModeIsolines;
		} else {
			errorLog = "Neither tessellation shader specifies a patch input mode (Triangles, Quads, or Isolines).";
			return false;
		}

		if (tescModes.get(spv::ExecutionModeVertexOrderCw)) {
			reflectData.windingOrder = spv::ExecutionModeVertexOrderCw;
		} else if (tescModes.get(spv::ExecutionModeVertexOrderCcw)) {
			reflectData.windingOrder = spv::ExecutionModeVertexOrderCcw;
		} else if (teseModes.get(spv::ExecutionModeVertexOrderCw)) {
			reflectData.windingOrder = spv::ExecutionModeVertexOrderCw;
		} else if (teseModes.get(spv::ExecutionModeVertexOrderCcw)) {
			reflectData.windingOrder = spv::ExecutionModeVertexOrderCcw;
		} else {
			errorLog = "Neither tessellation shader specifies a winding order mode (VertexOrderCw or VertexOrderCcw).";
			return false;
		}

		reflectData.pointMode = tescModes.get(spv::ExecutionModePointMode) || teseModes.get(spv::ExecutionModePointMode);

		if (tescModes.get(spv::ExecutionModeSpacingEqual)) {
			reflectData.partitionMode = spv::ExecutionModeSpacingEqual;
		} else if (tescModes.get(spv::ExecutionModeSpacingFractionalEven)) {
			reflectData.partitionMode = spv::ExecutionModeSpacingFractionalEven;
		} else if (tescModes.get(spv::ExecutionModeSpacingFractionalOdd)) {
			reflectData.partitionMode = spv::ExecutionModeSpacingFractionalOdd;
		} else if (teseModes.get(spv::ExecutionModeSpacingEqual)) {
			reflectData.partitionMode = spv::ExecutionModeSpacingEqual;
		} else if (teseModes.get(spv::ExecutionModeSpacingFractionalEven)) {
			reflectData.partitionMode = spv::ExecutionModeSpacingFractionalEven;
		} else if (teseModes.get(spv::ExecutionModeSpacingFractionalOdd)) {
			reflectData.partitionMode = spv::ExecutionModeSpacingFractionalOdd;
		} else {
			errorLog = "Neither tessellation shader specifies a partition mode (SpacingEqual, SpacingFractionalOdd, or SpacingFractionalEven).";
			return false;
		}

		if (tescModes.get(spv::ExecutionModeOutputVertices)) {
			reflectData.numControlPoints = tesc.outputVertices;
		} else if (teseModes.get(spv::ExecutionModeOutputVertices)) {
			reflectData.numControlPoints = tese.outputVertices;
		} else {
			errorLog = "Neither tessellation shader specifies the number of output control points.";
			return false;
		}

		return true;
	}

	/** Returns the size in bytes of the interface variable. */
	static inline uint32_t getShaderInterfaceVariableSize(const SPIRVShaderInterfaceVariable& var) {
		if ( !var.isUsed ) { return 0; }		// Unused variables consume no buffer space.

		uint32_t vecWidth = var.vecWidth;
		if (vecWidth == 3) { vecWidth = 4; }	// Metal 3-vectors consume same as 4-vectors.
		switch (var.baseType) {
			case SPIRV_CROSS_NAMESPACE::SPIRType::SByte:
			case SPIRV_CROSS_NAMESPACE::SPIRType::UByte:
				return 1 * vecWidth;
			case SPIRV_CROSS_NAMESPACE::SPIRType::Short:
			case SPIRV_CROSS_NAMESPACE::SPIRType::UShort:
			case SPIRV_CROSS_NAMESPACE::SPIRType::Half:
				return 2 * vecWidth;
			case SPIRV_CROSS_NAMESPACE::SPIRType::Int:
			case SPIRV_CROSS_NAMESPACE::SPIRType::UInt:
			case SPIRV_CROSS_NAMESPACE::SPIRType::Float:
			default:
				return 4 * vecWidth;
		}
	}
	static inline uint32_t getShaderOutputSize(const SPIRVShaderOutput& output) {
		return getShaderInterfaceVariableSize(output);
	}

	/**
	 * Returns the alignment of the shader interface variable, which typically matches the size of the variable,
	 * but the first member of a nested struct may inherit special alignment from the struct.
	 */
	static inline uint32_t getShaderInterfaceVariableAlignment(const SPIRVShaderInterfaceVariable& var) {
		if(var.firstStructMemberAlignment && var.isUsed) {
			return var.firstStructMemberAlignment;
		} else {
			return getShaderOutputSize(var);
		}
	}
	static inline uint32_t getShaderOutputAlignment(const SPIRVShaderOutput& output) {
		return getShaderInterfaceVariableAlignment(output);
	}

	/** Index value meaning no interface variable. */
	static constexpr size_t kSPIRVNoInterfaceVariable = size_t(-1);

	auto addSat = [](uint32_t a, uint32_t b) { return a == uint32_t(-1) ? a : a + b; };

	/** Array-sizing specialization IDs and scalar values; up to eight values remain inline. */
	using SPIRVSpecializationValues = SPIRV_CROSS_NAMESPACE::SmallVector<std::pair<uint32_t, uint64_t>, 8>;

	/** Applies scalar specialization values before evaluating interface arrays or converting MSL. */
	static inline void setSpecializationValues(SPIRV_CROSS_NAMESPACE::Compiler& compiler, const SPIRVSpecializationValues& values) {
		for (const auto& sc : compiler.get_specialization_constants()) {
			for (const auto& value : values) {
				if (value.first == sc.constant_id) { compiler.get_constant(sc.id).m.c[0].r[0].u64 = value.second; }
			}
		}
	}

	/** Retains defaults for specialization constants used by array lengths, including expression dependencies. */
	template<typename Vs>
	static inline bool getArraySizeSpecializationConstants(const Vs& spirv, SPIRVSpecializationValues& values, std::string& errorLog) {
#ifndef SPIRV_CROSS_EXCEPTIONS_TO_ASSERTIONS
		try {
#endif
			SPIRV_CROSS_NAMESPACE::Compiler compiler(spirv);
			SPIRVSpecializationValues defaults;
			for (const auto& sc : compiler.get_specialization_constants()) {
				const auto& constant = compiler.get_constant(sc.id);
				if (constant.is_used_as_array_length) { defaults.emplace_back(sc.constant_id, constant.m.c[0].r[0].u64); }
			}
			std::sort(defaults.begin(), defaults.end());
			values = std::move(defaults);
			return true;
#ifndef SPIRV_CROSS_EXCEPTIONS_TO_ASSERTIONS
		} catch (SPIRV_CROSS_NAMESPACE::CompilerError& ex) {
			errorLog = ex.what();
			return false;
		}
#endif
	}

	static inline uint32_t getElementCount(const SPIRV_CROSS_NAMESPACE::CompilerReflection& reflect,
										 const SPIRV_CROSS_NAMESPACE::SPIRType* type) {
		uint32_t count = type->array.empty() ? 1 : type->array_size_literal[0] ? type->array[0] : reflect.evaluate_constant_u32(type->array[0]);
		return count * type->columns;
	}

	template<typename Vi>
	static inline uint32_t getShaderInterfaceStructMembers(const SPIRV_CROSS_NAMESPACE::CompilerReflection& reflect,
														   Vi& vars, size_t parentFirstMemberIdx,
														   const SPIRV_CROSS_NAMESPACE::SPIRType* structType, spv::StorageClass storage,
														   bool patch, uint32_t loc) {
		bool isUsed = true;
		auto biType = spv::BuiltInMax;
		// Track the first member by index, because adding to vars may reallocate it.
		size_t firstMemberIdx = kSPIRVNoInterfaceVariable;
		size_t mbrCnt = structType->member_types.size();
		for (uint32_t mbrIdx = 0; mbrIdx < mbrCnt; mbrIdx++) {
			// Each member may have a location decoration. If not, each member
			// gets an incrementing location based on the base location for the struct.
			uint32_t cmp = 0;
			if (reflect.has_member_decoration(structType->self, mbrIdx, spv::DecorationLocation)) {
				loc = reflect.get_member_decoration(structType->self, mbrIdx, spv::DecorationLocation);
				cmp = reflect.get_member_decoration(structType->self, mbrIdx, spv::DecorationComponent);
			}
			patch = patch || reflect.has_member_decoration(structType->self, mbrIdx, spv::DecorationPatch);
			if (reflect.has_member_decoration(structType->self, mbrIdx, spv::DecorationBuiltIn)) {
				biType = (spv::BuiltIn)reflect.get_member_decoration(structType->self, mbrIdx, spv::DecorationBuiltIn);
				isUsed = reflect.has_active_builtin(biType, storage);
			}
			const SPIRV_CROSS_NAMESPACE::SPIRType* type = &reflect.get_type(structType->member_types[mbrIdx]);
			uint32_t elemCnt = getElementCount(reflect, type);
			for (uint32_t elemIdx = 0; elemIdx < elemCnt; elemIdx++) {
				if (type->basetype == SPIRV_CROSS_NAMESPACE::SPIRType::Struct)
					loc = getShaderInterfaceStructMembers(reflect, vars, firstMemberIdx, type, storage, patch, loc);
				else {
					// The alignment of a structure is the same as the largest member of the structure.
					// Consequently, the first flattened member of a structure should align with structure itself.
					vars.push_back({type->basetype, type->vecsize, loc, cmp, 0, biType, patch, isUsed});
					if (firstMemberIdx == kSPIRVNoInterfaceVariable) { firstMemberIdx = vars.size() - 1; }
					auto& firstMember = vars[firstMemberIdx];
					firstMember.firstStructMemberAlignment = std::max(firstMember.firstStructMemberAlignment, getShaderOutputSize(vars.back()));
					loc = addSat(loc, 1);
				}
			}
		}

		// Set the parent's first member alignment to the largest alignment found so far.
		if (parentFirstMemberIdx != kSPIRVNoInterfaceVariable && firstMemberIdx != kSPIRVNoInterfaceVariable) {
			auto& parentFirstMember = vars[parentFirstMemberIdx];
			parentFirstMember.firstStructMemberAlignment = std::max(parentFirstMember.firstStructMemberAlignment, vars[firstMemberIdx].firstStructMemberAlignment);
		}

		return loc;
	}
	template<typename Vo>
	static inline uint32_t getShaderOutputStructMembers(const SPIRV_CROSS_NAMESPACE::CompilerReflection& reflect,
														Vo& outputs, size_t parentFirstMemberIdx,
														const SPIRV_CROSS_NAMESPACE::SPIRType* structType, spv::StorageClass storage,
														bool patch, uint32_t loc) {
		return getShaderInterfaceStructMembers(reflect, outputs, parentFirstMemberIdx, structType, storage, patch, loc);
	}

	/** Given a shader in SPIR-V format, returns interface reflection data. */
	template<typename Vs, typename Vi>
	static inline bool getShaderInterfaceVariables(const Vs& spirv, spv::StorageClass storage, spv::ExecutionModel model,
												   const std::string& entryName, Vi& vars, std::string& errorLog,
												   const SPIRVSpecializationValues& specValues = {}) {
#ifndef SPIRV_CROSS_EXCEPTIONS_TO_ASSERTIONS
		try {
#endif
			SPIRV_CROSS_NAMESPACE::Parser parser(spirv);
			parser.parse();
			SPIRV_CROSS_NAMESPACE::CompilerReflection reflect(parser.get_parsed_ir());
			if (!entryName.empty()) {
				reflect.set_entry_point(entryName, model);
			}
				setSpecializationValues(reflect, specValues);
			reflect.compile();
			reflect.update_active_builtins();

			vars.clear();

			for (auto varID : reflect.get_active_interface_variables()) {
				if (storage != reflect.get_storage_class(varID)) { continue; }

				bool isUsed = true;
				const auto* type = &reflect.get_type(reflect.get_type_from_variable(varID).parent_type);
				bool patch = reflect.has_decoration(varID, spv::DecorationPatch);
				if (reflect.has_decoration(type->self, spv::DecorationBlock)) {
					// In this case, the Patch decoration is on the members.
					// FIXME It is theoretically possible for some members of a block to have
					// the decoration and some not. What then?
					patch = reflect.has_member_decoration(type->self, 0, spv::DecorationPatch);
				}
				auto biType = spv::BuiltInMax;
				if (reflect.has_decoration(varID, spv::DecorationBuiltIn)) {
					biType = (spv::BuiltIn)reflect.get_decoration(varID, spv::DecorationBuiltIn);
					isUsed = reflect.has_active_builtin(biType, storage);
				}
				uint32_t loc = -1;
				uint32_t cmp = 0;
				if (reflect.has_decoration(varID, spv::DecorationLocation)) {
					loc = reflect.get_decoration(varID, spv::DecorationLocation);
				}
				if (reflect.has_decoration(varID, spv::DecorationComponent)) {
					cmp = reflect.get_decoration(varID, spv::DecorationComponent);
				}
				// For tessellation shaders, peel away the initial array type. SPIRV-Cross adds the array back automatically.
				// Only some builtins will be arrayed here.
				if ((model == spv::ExecutionModelTessellationControl || (model == spv::ExecutionModelTessellationEvaluation && storage == spv::StorageClassInput)) && !patch &&
					(biType == spv::BuiltInMax || biType == spv::BuiltInPosition || biType == spv::BuiltInPointSize ||
					 biType == spv::BuiltInClipDistance || biType == spv::BuiltInCullDistance))
					type = &reflect.get_type(type->parent_type);

					uint32_t elemCnt = getElementCount(reflect, type);
				for (uint32_t i = 0; i < elemCnt; i++) {
					if (type->basetype == SPIRV_CROSS_NAMESPACE::SPIRType::Struct) {
						loc = getShaderInterfaceStructMembers(reflect, vars, kSPIRVNoInterfaceVariable, type, storage, patch, loc);
					} else {
						vars.push_back({type->basetype, type->vecsize, loc, cmp, 0, biType, patch, isUsed});
						loc = addSat(loc, 1);
					}
				}
			}
			// Sort variables by ascending location.
			std::stable_sort(vars.begin(), vars.end(), [](const SPIRVShaderInterfaceVariable& a, const SPIRVShaderInterfaceVariable& b) {
				return a.location < b.location;
			});
			// Assign locations to variables that don't have one.
			uint32_t loc = -1;
			for (SPIRVShaderInterfaceVariable& var : vars) {
				if (var.location == uint32_t(-1)) { var.location = loc + 1; }
				loc = var.location;
			}
			return true;
#ifndef SPIRV_CROSS_EXCEPTIONS_TO_ASSERTIONS
		} catch (SPIRV_CROSS_NAMESPACE::CompilerError& ex) {
			errorLog = ex.what();
			return false;
		}
#endif
	}
	template<typename Vs, typename Vo>
	static inline bool getShaderOutputs(const Vs& spirv, spv::ExecutionModel model, const std::string& entryName,
										Vo& outputs, std::string& errorLog, const SPIRVSpecializationValues& specValues = {}) {
		return getShaderInterfaceVariables(spirv, spv::StorageClassOutput, model, entryName, outputs, errorLog, specValues);
	}
	template<typename Vs, typename Vo>
	static inline bool getShaderInputs(const Vs& spirv, spv::ExecutionModel model, const std::string& entryName,
										Vo& outputs, std::string& errorLog, const SPIRVSpecializationValues& specValues = {}) {
		return getShaderInterfaceVariables(spirv, spv::StorageClassInput, model, entryName, outputs, errorLog, specValues);
	}

}
#endif
