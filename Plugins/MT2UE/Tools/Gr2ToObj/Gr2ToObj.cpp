/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include <windows.h>

#include "granny.h"

struct GrannyPNT3322Vertex
{
    granny_real32 Position[3];
    granny_real32 Normal[3];
    granny_real32 UV0[2];
    granny_real32 UV1[2];
};

static granny_data_type_definition GrannyPNT3322VertexType[5] =
{
    { GrannyReal32Member, GrannyVertexPositionName, 0, 3 },
    { GrannyReal32Member, GrannyVertexNormalName, 0, 3 },
    { GrannyReal32Member, GrannyVertexTextureCoordinatesName "0", 0, 2 },
    { GrannyReal32Member, GrannyVertexTextureCoordinatesName "1", 0, 2 },
    { GrannyEndMember }
};

struct MaterialInfo
{
    std::string Name;
    std::string DiffuseTexture;
    std::string OpacityTexture;
};

struct MeshExport
{
    granny_mesh* Mesh = nullptr;
    std::string Name;
    std::vector<GrannyPNT3322Vertex> Vertices;
    std::vector<uint32_t> Indices;
};

static std::string NormalizeSlashes(std::string Value)
{
    std::replace(Value.begin(), Value.end(), '\\', '/');
    return Value;
}

static std::string TrimTrailingSlash(std::string Value)
{
    Value = NormalizeSlashes(Value);
    while (!Value.empty() && Value.back() == '/')
    {
        Value.pop_back();
    }
    return Value;
}

static bool FileExists(const std::string& Path)
{
    const DWORD Attributes = GetFileAttributesA(Path.c_str());
    return Attributes != INVALID_FILE_ATTRIBUTES && (Attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

static bool DirectoryExists(const std::string& Path)
{
    const DWORD Attributes = GetFileAttributesA(Path.c_str());
    return Attributes != INVALID_FILE_ATTRIBUTES && (Attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

static std::string BaseName(const std::string& Path)
{
    const std::string Normalized = NormalizeSlashes(Path);
    const size_t Slash = Normalized.find_last_of('/');
    return Slash == std::string::npos ? Normalized : Normalized.substr(Slash + 1);
}

static std::string DirectoryName(const std::string& Path)
{
    const std::string Normalized = NormalizeSlashes(Path);
    const size_t Slash = Normalized.find_last_of('/');
    return Slash == std::string::npos ? std::string(".") : Normalized.substr(0, Slash);
}

static std::string JoinPath(const std::string& Left, const std::string& Right)
{
    if (Left.empty())
    {
        return NormalizeSlashes(Right);
    }
    if (Right.empty())
    {
        return NormalizeSlashes(Left);
    }
    return TrimTrailingSlash(Left) + "/" + NormalizeSlashes(Right);
}

static std::string RemoveExtension(const std::string& Path)
{
    const std::string Name = BaseName(Path);
    const size_t Dot = Name.find_last_of('.');
    return Dot == std::string::npos ? Name : Name.substr(0, Dot);
}

static std::string SanitizeName(const std::string& Value, const std::string& Fallback)
{
    std::string Result;
    Result.reserve(Value.size());

    for (char Ch : Value)
    {
        const unsigned char UCh = static_cast<unsigned char>(Ch);
        if (std::isalnum(UCh) || Ch == '_' || Ch == '-')
        {
            Result.push_back(Ch);
        }
        else
        {
            Result.push_back('_');
        }
    }

    while (!Result.empty() && Result.front() == '_')
    {
        Result.erase(Result.begin());
    }
    while (!Result.empty() && Result.back() == '_')
    {
        Result.pop_back();
    }

    return Result.empty() ? Fallback : Result;
}

static bool StartsWithCaseInsensitive(const std::string& Value, const char* Prefix)
{
    const size_t PrefixLength = std::strlen(Prefix);
    if (Value.size() < PrefixLength)
    {
        return false;
    }

    for (size_t Index = 0; Index < PrefixLength; ++Index)
    {
        const unsigned char A = static_cast<unsigned char>(Value[Index]);
        const unsigned char B = static_cast<unsigned char>(Prefix[Index]);
        if (std::tolower(A) != std::tolower(B))
        {
            return false;
        }
    }

    return true;
}

static std::string TextureFileName(granny_texture* Texture)
{
    if (!Texture || !Texture->FromFileName)
    {
        return std::string();
    }

    return NormalizeSlashes(Texture->FromFileName);
}

static bool StartsWithCaseInsensitiveString(const std::string& Value, const std::string& Prefix)
{
    if (Value.size() < Prefix.size())
    {
        return false;
    }

    for (size_t Index = 0; Index < Prefix.size(); ++Index)
    {
        const unsigned char A = static_cast<unsigned char>(Value[Index]);
        const unsigned char B = static_cast<unsigned char>(Prefix[Index]);
        if (std::tolower(A) != std::tolower(B))
        {
            return false;
        }
    }

    return true;
}

static bool TryFindUnderFirstLevelPacks(const std::string& Root, const std::string& RelativePath, std::string& OutPath)
{
    const std::string SearchPattern = JoinPath(Root, "*");
    WIN32_FIND_DATAA FindData;
    HANDLE FindHandle = FindFirstFileA(SearchPattern.c_str(), &FindData);
    if (FindHandle == INVALID_HANDLE_VALUE)
    {
        return false;
    }

    do
    {
        if ((FindData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
        {
            continue;
        }

        const std::string Name = FindData.cFileName;
        if (Name == "." || Name == "..")
        {
            continue;
        }

        const std::string Candidate = JoinPath(JoinPath(Root, Name), RelativePath);
        if (FileExists(Candidate))
        {
            OutPath = Candidate;
            FindClose(FindHandle);
            return true;
        }
    }
    while (FindNextFileA(FindHandle, &FindData));

    FindClose(FindHandle);
    return false;
}

static std::string StripVirtualDrivePrefix(const std::string& Path)
{
    std::string Normalized = NormalizeSlashes(Path);
    if (Normalized.size() > 3 && Normalized[1] == ':' && Normalized[2] == '/')
    {
        Normalized = Normalized.substr(3);
    }
    while (!Normalized.empty() && Normalized.front() == '/')
    {
        Normalized.erase(Normalized.begin());
    }
    return Normalized;
}

static std::string ResolveTexturePath(const std::string& TexturePath, const std::string& InputPath, const std::string& ExtractedRoot)
{
    if (TexturePath.empty())
    {
        return TexturePath;
    }

    const std::string NormalizedTexture = NormalizeSlashes(TexturePath);
    if (FileExists(NormalizedTexture))
    {
        return NormalizedTexture;
    }

    const std::string InputRelativeCandidate = JoinPath(DirectoryName(InputPath), NormalizedTexture);
    if (FileExists(InputRelativeCandidate))
    {
        return InputRelativeCandidate;
    }

    if (ExtractedRoot.empty() || !DirectoryExists(ExtractedRoot))
    {
        return NormalizedTexture;
    }

    const std::string Root = TrimTrailingSlash(ExtractedRoot);
    const std::string StrippedVirtualPath = StripVirtualDrivePrefix(NormalizedTexture);

    const std::string DirectCandidate = JoinPath(Root, StrippedVirtualPath);
    if (FileExists(DirectCandidate))
    {
        return DirectCandidate;
    }

    std::string FoundPath;
    if (TryFindUnderFirstLevelPacks(Root, StrippedVirtualPath, FoundPath))
    {
        return NormalizeSlashes(FoundPath);
    }

    if (StartsWithCaseInsensitiveString(StrippedVirtualPath, "ymir work/"))
    {
        const std::string WithoutYmir = StrippedVirtualPath.substr(std::strlen("ymir work/"));
        if (TryFindUnderFirstLevelPacks(Root, WithoutYmir, FoundPath))
        {
            return NormalizeSlashes(FoundPath);
        }
    }

    return NormalizedTexture;
}

static MaterialInfo ReadMaterial(granny_material* Material, int MaterialIndex, const std::string& InputPath, const std::string& ExtractedRoot)
{
    MaterialInfo Info;
    std::ostringstream Fallback;
    Fallback << "material_" << MaterialIndex;
    Info.Name = SanitizeName(Material && Material->Name ? Material->Name : std::string(), Fallback.str());

    granny_texture* Diffuse = nullptr;
    granny_texture* Opacity = nullptr;

    if (Material)
    {
        if (Material->MapCount > 1 && Material->Maps && Material->Name && StartsWithCaseInsensitive(Material->Name, "Blend"))
        {
            Diffuse = GrannyGetMaterialTextureByType(Material->Maps[0].Material, GrannyDiffuseColorTexture);
            Opacity = GrannyGetMaterialTextureByType(Material->Maps[1].Material, GrannyDiffuseColorTexture);
        }
        else
        {
            Diffuse = GrannyGetMaterialTextureByType(Material, GrannyDiffuseColorTexture);
            Opacity = GrannyGetMaterialTextureByType(Material, GrannyOpacityTexture);
        }
    }

    Info.DiffuseTexture = ResolveTexturePath(TextureFileName(Diffuse), InputPath, ExtractedRoot);
    Info.OpacityTexture = ResolveTexturePath(TextureFileName(Opacity), InputPath, ExtractedRoot);
    return Info;
}

static std::string UniqueMaterialName(const std::string& Base, std::set<std::string>& UsedNames)
{
    std::string Candidate = Base;
    int Suffix = 2;
    while (UsedNames.find(Candidate) != UsedNames.end())
    {
        std::ostringstream WithSuffix;
        WithSuffix << Base << "_" << Suffix++;
        Candidate = WithSuffix.str();
    }
    UsedNames.insert(Candidate);
    return Candidate;
}

static void CollectModelMeshes(granny_file_info* Info, std::vector<granny_mesh*>& OutMeshes)
{
    std::set<granny_mesh*> Seen;

    for (int ModelIndex = 0; ModelIndex < Info->ModelCount; ++ModelIndex)
    {
        granny_model* Model = Info->Models[ModelIndex];
        if (!Model || !Model->MeshBindings)
        {
            continue;
        }

        for (int BindingIndex = 0; BindingIndex < Model->MeshBindingCount; ++BindingIndex)
        {
            granny_mesh* Mesh = Model->MeshBindings[BindingIndex].Mesh;
            if (Mesh && Seen.insert(Mesh).second)
            {
                OutMeshes.push_back(Mesh);
            }
        }
    }

    if (!OutMeshes.empty())
    {
        return;
    }

    for (int MeshIndex = 0; MeshIndex < Info->MeshCount; ++MeshIndex)
    {
        granny_mesh* Mesh = Info->Meshes[MeshIndex];
        if (Mesh && Seen.insert(Mesh).second)
        {
            OutMeshes.push_back(Mesh);
        }
    }
}

static bool ExportMesh(granny_mesh* Mesh, int MeshIndex, MeshExport& OutMesh)
{
    const int VertexCount = GrannyGetMeshVertexCount(Mesh);
    const int IndexCount = GrannyGetMeshIndexCount(Mesh);
    if (VertexCount <= 0 || IndexCount <= 0)
    {
        return false;
    }

    OutMesh.Mesh = Mesh;
    std::ostringstream Fallback;
    Fallback << "mesh_" << MeshIndex;
    OutMesh.Name = SanitizeName(Mesh->Name ? Mesh->Name : std::string(), Fallback.str());
    OutMesh.Vertices.resize(VertexCount);
    OutMesh.Indices.resize(IndexCount);

    GrannyCopyMeshVertices(Mesh, GrannyPNT3322VertexType, &OutMesh.Vertices[0]);
    GrannyCopyMeshIndices(Mesh, sizeof(uint32_t), &OutMesh.Indices[0]);
    return true;
}

static bool WriteMtl(const std::string& MtlPath, const std::vector<MaterialInfo>& Materials)
{
    std::ofstream Mtl(MtlPath.c_str(), std::ios::out | std::ios::trunc);
    if (!Mtl)
    {
        return false;
    }

    Mtl << "# Generated by MT2UE Gr2ToObj\n\n";
    for (const MaterialInfo& Material : Materials)
    {
        Mtl << "newmtl " << Material.Name << "\n";
        Mtl << "Ka 1.000000 1.000000 1.000000\n";
        Mtl << "Kd 1.000000 1.000000 1.000000\n";
        Mtl << "Ks 0.000000 0.000000 0.000000\n";
        Mtl << "d 1.000000\n";
        if (!Material.DiffuseTexture.empty())
        {
            Mtl << "map_Kd " << Material.DiffuseTexture << "\n";
        }
        if (!Material.OpacityTexture.empty())
        {
            Mtl << "map_d " << Material.OpacityTexture << "\n";
        }
        Mtl << "\n";
    }

    return true;
}

static void WriteFace(std::ofstream& Obj, uint32_t A, uint32_t B, uint32_t C, uint32_t VertexOffset)
{
    const uint32_t IA = VertexOffset + A + 1;
    const uint32_t IB = VertexOffset + B + 1;
    const uint32_t IC = VertexOffset + C + 1;
    Obj << "f "
        << IA << "/" << IA << "/" << IA << " "
        << IB << "/" << IB << "/" << IB << " "
        << IC << "/" << IC << "/" << IC << "\n";
}

static bool WriteObj(
    const std::string& ObjPath,
    const std::string& MtlPath,
    const std::vector<MeshExport>& Meshes,
    const std::map<granny_material*, std::string>& MaterialNames)
{
    std::ofstream Obj(ObjPath.c_str(), std::ios::out | std::ios::trunc);
    if (!Obj)
    {
        return false;
    }

    Obj << "# Generated by MT2UE Gr2ToObj\n";
    Obj << "mtllib " << BaseName(MtlPath) << "\n\n";
    Obj << std::fixed << std::setprecision(6);

    uint32_t VertexOffset = 0;
    for (const MeshExport& Mesh : Meshes)
    {
        Obj << "o " << Mesh.Name << "\n";
        for (const GrannyPNT3322Vertex& Vertex : Mesh.Vertices)
        {
            Obj << "v " << Vertex.Position[0] << " " << Vertex.Position[1] << " " << Vertex.Position[2] << "\n";
        }
        for (const GrannyPNT3322Vertex& Vertex : Mesh.Vertices)
        {
            Obj << "vt " << Vertex.UV0[0] << " " << (1.0f - Vertex.UV0[1]) << "\n";
        }
        for (const GrannyPNT3322Vertex& Vertex : Mesh.Vertices)
        {
            Obj << "vn " << Vertex.Normal[0] << " " << Vertex.Normal[1] << " " << Vertex.Normal[2] << "\n";
        }

        const int GroupCount = GrannyGetMeshTriangleGroupCount(Mesh.Mesh);
        const granny_tri_material_group* Groups = GrannyGetMeshTriangleGroups(Mesh.Mesh);
        if (GroupCount > 0 && Groups)
        {
            for (int GroupIndex = 0; GroupIndex < GroupCount; ++GroupIndex)
            {
                const granny_tri_material_group& Group = Groups[GroupIndex];
                granny_material* Material = nullptr;
                if (Group.MaterialIndex >= 0 && Group.MaterialIndex < Mesh.Mesh->MaterialBindingCount)
                {
                    Material = Mesh.Mesh->MaterialBindings[Group.MaterialIndex].Material;
                }

                const auto FoundName = MaterialNames.find(Material);
                if (FoundName != MaterialNames.end())
                {
                    Obj << "usemtl " << FoundName->second << "\n";
                }

                const int FirstIndex = Group.TriFirst * 3;
                const int LastIndex = FirstIndex + Group.TriCount * 3;
                for (int Index = FirstIndex; Index + 2 < LastIndex && Index + 2 < static_cast<int>(Mesh.Indices.size()); Index += 3)
                {
                    WriteFace(Obj, Mesh.Indices[Index], Mesh.Indices[Index + 1], Mesh.Indices[Index + 2], VertexOffset);
                }
            }
        }
        else
        {
            for (int Index = 0; Index + 2 < static_cast<int>(Mesh.Indices.size()); Index += 3)
            {
                WriteFace(Obj, Mesh.Indices[Index], Mesh.Indices[Index + 1], Mesh.Indices[Index + 2], VertexOffset);
            }
        }

        VertexOffset += static_cast<uint32_t>(Mesh.Vertices.size());
        Obj << "\n";
    }

    return true;
}

int main(int Argc, char** Argv)
{
    if (Argc < 3)
    {
        std::cerr << "Usage: Gr2ToObj.exe input.gr2 output.obj [extracted_pack_root]\n";
        return 2;
    }

    const std::string InputPath = Argv[1];
    const std::string ObjPath = Argv[2];
    const std::string ExtractedRoot = Argc >= 4 ? TrimTrailingSlash(Argv[3]) : std::string();
    const std::string MtlPath = DirectoryName(ObjPath) + "/" + RemoveExtension(ObjPath) + ".mtl";

    granny_file* File = GrannyReadEntireFile(InputPath.c_str());
    if (!File)
    {
        std::cerr << "Failed to read Granny file: " << InputPath << "\n";
        return 1;
    }

    granny_file_info* Info = GrannyGetFileInfo(File);
    if (!Info)
    {
        std::cerr << "Failed to read Granny file info: " << InputPath << "\n";
        GrannyFreeFile(File);
        return 1;
    }

    std::vector<granny_mesh*> SourceMeshes;
    CollectModelMeshes(Info, SourceMeshes);
    if (SourceMeshes.empty())
    {
        std::cerr << "No meshes found: " << InputPath << "\n";
        GrannyFreeFile(File);
        return 1;
    }

    std::vector<MeshExport> Meshes;
    Meshes.reserve(SourceMeshes.size());
    for (size_t Index = 0; Index < SourceMeshes.size(); ++Index)
    {
        MeshExport Exported;
        if (ExportMesh(SourceMeshes[Index], static_cast<int>(Index), Exported))
        {
            Meshes.push_back(Exported);
        }
    }

    if (Meshes.empty())
    {
        std::cerr << "No exportable meshes found: " << InputPath << "\n";
        GrannyFreeFile(File);
        return 1;
    }

    std::vector<MaterialInfo> Materials;
    std::map<granny_material*, std::string> MaterialNames;
    std::set<std::string> UsedMaterialNames;
    for (int MaterialIndex = 0; MaterialIndex < Info->MaterialCount; ++MaterialIndex)
    {
        granny_material* Material = Info->Materials[MaterialIndex];
        MaterialInfo MaterialData = ReadMaterial(Material, MaterialIndex, InputPath, ExtractedRoot);
        MaterialData.Name = UniqueMaterialName(MaterialData.Name, UsedMaterialNames);
        MaterialNames[Material] = MaterialData.Name;
        Materials.push_back(MaterialData);
    }

    if (Materials.empty())
    {
        MaterialInfo DefaultMaterial;
        DefaultMaterial.Name = "default";
        Materials.push_back(DefaultMaterial);
    }

    const bool bWroteMtl = WriteMtl(MtlPath, Materials);
    const bool bWroteObj = WriteObj(ObjPath, MtlPath, Meshes, MaterialNames);
    GrannyFreeFile(File);

    if (!bWroteMtl || !bWroteObj)
    {
        std::cerr << "Failed to write OBJ/MTL output: " << ObjPath << "\n";
        return 1;
    }

    std::cout << "Exported " << Meshes.size() << " mesh(es) to " << ObjPath << "\n";
    return 0;
}
