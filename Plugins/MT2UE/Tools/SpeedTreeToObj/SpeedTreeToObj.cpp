/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#define SPEEDTREERT_DYNAMIC_LIB
#include "SpeedTreeRT.h"

namespace
{
	struct ObjWriter
	{
		std::ofstream Stream;
		unsigned int VertexBase = 1;

		explicit ObjWriter(const char* Path) : Stream(Path, std::ios::binary) {}

		void Vertex(float X, float Y, float Z) { Stream << "v " << X << ' ' << Y << ' ' << Z << '\n'; }
		void Normal(float X, float Y, float Z) { Stream << "vn " << X << ' ' << Y << ' ' << Z << '\n'; }
		void TexCoord(float U, float V) { Stream << "vt " << U << ' ' << (1.0f - V) << '\n'; }
		void Face(unsigned int A, unsigned int B, unsigned int C)
		{
			Stream << "f " << A << '/' << A << '/' << A << ' '
				<< B << '/' << B << '/' << B << ' '
				<< C << '/' << C << '/' << C << '\n';
		}
	};

	std::string FileNameWithoutExtension(const char* Value)
	{
		if (!Value) return {};
		std::string Result(Value);
		const std::string::size_type Slash = Result.find_last_of("/\\");
		if (Slash != std::string::npos) Result.erase(0, Slash + 1);
		const std::string::size_type Dot = Result.find_last_of('.');
		if (Dot != std::string::npos) Result.erase(Dot);
		return Result;
	}

	void WriteIndexedGeometry(ObjWriter& Writer, const CSpeedTreeRT::SGeometry::SIndexed& Geometry,
		const char* GroupName, const char* MaterialName)
	{
		if (Geometry.m_usVertexCount == 0 || Geometry.m_usNumStrips == 0) return;
		Writer.Stream << "g " << GroupName << "\nusemtl " << MaterialName << '\n';

		const unsigned int Base = Writer.VertexBase;
		for (unsigned int Index = 0; Index < Geometry.m_usVertexCount; ++Index)
		{
			Writer.Vertex(Geometry.m_pCoords[Index * 3], Geometry.m_pCoords[Index * 3 + 1], Geometry.m_pCoords[Index * 3 + 2]);
			Writer.Normal(Geometry.m_pNormals[Index * 3], Geometry.m_pNormals[Index * 3 + 1], Geometry.m_pNormals[Index * 3 + 2]);
			Writer.TexCoord(Geometry.m_pTexCoords0[Index * 2], Geometry.m_pTexCoords0[Index * 2 + 1]);
		}

		for (unsigned int StripIndex = 0; StripIndex < Geometry.m_usNumStrips; ++StripIndex)
		{
			const unsigned short* Strip = Geometry.m_pStrips[StripIndex];
			const unsigned int Length = Geometry.m_pStripLengths[StripIndex];
			for (unsigned int Index = 2; Index < Length; ++Index)
			{
				unsigned int A = Strip[Index - 2];
				unsigned int B = Strip[Index - 1];
				unsigned int C = Strip[Index];
				if ((Index & 1u) != 0u) std::swap(A, B);
				if (A == B || B == C || C == A) continue;
				Writer.Face(Base + A, Base + B, Base + C);
			}
		}
		Writer.VertexBase += Geometry.m_usVertexCount;
	}

	void WriteLeaves(ObjWriter& Writer, const CSpeedTreeRT::SGeometry::SLeaf& Leaves, float Size)
	{
		if (!Leaves.m_bIsActive || Leaves.m_usLeafCount == 0) return;
		Writer.Stream << "g Leaves\nusemtl Foliage\n";
		static const unsigned int Corners[6] = { 0, 1, 2, 0, 2, 3 };

		for (unsigned int LeafIndex = 0; LeafIndex < Leaves.m_usLeafCount; ++LeafIndex)
		{
			const unsigned int Base = Writer.VertexBase;
			const float* Center = Leaves.m_pCenterCoords + LeafIndex * 3;
			const float* Coords = Leaves.m_pLeafMapCoords[LeafIndex];
			const float* UVs = Leaves.m_pLeafMapTexCoords[LeafIndex];
			for (unsigned int Corner = 0; Corner < 4; ++Corner)
			{
				Writer.Vertex(Center[0] + Coords[Corner * 4] * Size,
					Center[1] + Coords[Corner * 4 + 1] * Size,
					Center[2] + Coords[Corner * 4 + 2] * Size);
				Writer.Normal(Leaves.m_pNormals[LeafIndex * 3], Leaves.m_pNormals[LeafIndex * 3 + 1], Leaves.m_pNormals[LeafIndex * 3 + 2]);
				Writer.TexCoord(UVs[Corner * 2], UVs[Corner * 2 + 1]);
			}
			Writer.Face(Base + Corners[0], Base + Corners[1], Base + Corners[2]);
			Writer.Face(Base + Corners[3], Base + Corners[4], Base + Corners[5]);
			Writer.VertexBase += 4;
		}
	}

	bool WriteMaterialManifest(const char* Path, const CSpeedTreeRT::STextures& Textures)
	{
		std::ofstream Output(Path, std::ios::binary);
		if (!Output) return false;
		Output << "Branch=" << FileNameWithoutExtension(Textures.m_pBranchTextureFilename) << ".dds\n";
		const char* FoliageTexture = Textures.m_pCompositeFilename;
		if (!FoliageTexture && Textures.m_uiLeafTextureCount > 0) FoliageTexture = Textures.m_pLeafTextureFilenames[0];
		if (!FoliageTexture && Textures.m_uiFrondTextureCount > 0) FoliageTexture = Textures.m_pFrondTextureFilenames[0];
		Output << "Foliage=" << FileNameWithoutExtension(FoliageTexture) << ".dds\n";
		return true;
	}
}

int main(int ArgC, char** ArgV)
{
	if (ArgC < 4)
	{
		std::cerr << "Usage: SpeedTreeToObj input.spt output.obj output.materials\n";
		return 2;
	}

	CSpeedTreeRT Tree;
	Tree.SetTextureFlip(true);
	Tree.SetBranchLightingMethod(CSpeedTreeRT::LIGHT_DYNAMIC);
	Tree.SetLeafLightingMethod(CSpeedTreeRT::LIGHT_DYNAMIC);
	Tree.SetFrondLightingMethod(CSpeedTreeRT::LIGHT_DYNAMIC);
	Tree.SetBranchWindMethod(CSpeedTreeRT::WIND_NONE);
	Tree.SetLeafWindMethod(CSpeedTreeRT::WIND_NONE);
	Tree.SetFrondWindMethod(CSpeedTreeRT::WIND_NONE);
	Tree.SetNumLeafRockingGroups(1);

	if (!Tree.LoadTree(ArgV[1]) || !Tree.Compute(nullptr, 1))
	{
		std::cerr << CSpeedTreeRT::GetCurrentError() << '\n';
		return 3;
	}

	Tree.SetLodLevel(1.0f);
	CSpeedTreeRT::SGeometry Geometry;
	Tree.GetGeometry(Geometry, SpeedTree_AllGeometry, 0, 0, 0);
	CSpeedTreeRT::STextures Textures;
	Tree.GetTextures(Textures);

	ObjWriter Writer(ArgV[2]);
	if (!Writer.Stream)
	{
		std::cerr << "Could not create OBJ output.\n";
		return 4;
	}
	Writer.Stream << "# MT2UE_SPEEDTREE_CONVERSION_V1\n";
	WriteIndexedGeometry(Writer, Geometry.m_sBranches, "Branches", "Branch");
	WriteIndexedGeometry(Writer, Geometry.m_sFronds, "Fronds", "Foliage");
	const float* LeafSizes = Tree.GetLeafLodSizeAdjustments();
	WriteLeaves(Writer, Geometry.m_sLeaves0, LeafSizes ? LeafSizes[0] : 1.0f);
	Writer.Stream.flush();

	if (!Writer.Stream || !WriteMaterialManifest(ArgV[3], Textures))
	{
		std::cerr << "Could not finish converter output.\n";
		return 5;
	}
	return 0;
}
