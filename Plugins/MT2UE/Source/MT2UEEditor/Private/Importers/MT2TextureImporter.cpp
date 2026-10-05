/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "Importers/MT2TextureImporter.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetImportTask.h"
#include "AssetToolsModule.h"
#include "Engine/Texture2D.h"
#include "HAL/FileManager.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/ScopedSlowTask.h"
#include "Modules/ModuleManager.h"
#include "MT2AssetOptimizer.h"
#include "UObject/Package.h"

namespace
{
	constexpr uint32 MakeFourCC(char A, char B, char C, char D)
	{
		return static_cast<uint32>(static_cast<uint8>(A)) |
			(static_cast<uint32>(static_cast<uint8>(B)) << 8) |
			(static_cast<uint32>(static_cast<uint8>(C)) << 16) |
			(static_cast<uint32>(static_cast<uint8>(D)) << 24);
	}

	uint32 ReadU32(const TArray<uint8>& Data, int32 Offset)
	{
		if (!Data.IsValidIndex(Offset + 3))
		{
			return 0;
		}
		return static_cast<uint32>(Data[Offset]) |
			(static_cast<uint32>(Data[Offset + 1]) << 8) |
			(static_cast<uint32>(Data[Offset + 2]) << 16) |
			(static_cast<uint32>(Data[Offset + 3]) << 24);
	}

	uint16 ReadU16(const uint8* Data)
	{
		return static_cast<uint16>(Data[0]) | (static_cast<uint16>(Data[1]) << 8);
	}

	FColor ColorFrom565(uint16 Value)
	{
		const uint8 R5 = static_cast<uint8>((Value >> 11) & 0x1f);
		const uint8 G6 = static_cast<uint8>((Value >> 5) & 0x3f);
		const uint8 B5 = static_cast<uint8>(Value & 0x1f);
		return FColor(
			static_cast<uint8>((R5 << 3) | (R5 >> 2)),
			static_cast<uint8>((G6 << 2) | (G6 >> 4)),
			static_cast<uint8>((B5 << 3) | (B5 >> 2)),
			255);
	}

	void WritePixel(TArray<uint8>& OutBGRA, int32 Width, int32 Height, int32 X, int32 Y, const FColor& Color)
	{
		if (X < 0 || Y < 0 || X >= Width || Y >= Height)
		{
			return;
		}

		const int32 Offset = (Y * Width + X) * 4;
		OutBGRA[Offset + 0] = Color.B;
		OutBGRA[Offset + 1] = Color.G;
		OutBGRA[Offset + 2] = Color.R;
		OutBGRA[Offset + 3] = Color.A;
	}

	int32 MaskShift(uint32 Mask)
	{
		if (Mask == 0)
		{
			return 0;
		}

		int32 Shift = 0;
		while ((Mask & 1u) == 0u)
		{
			Mask >>= 1;
			Shift++;
		}
		return Shift;
	}

	int32 MaskBits(uint32 Mask)
	{
		int32 Bits = 0;
		while (Mask != 0)
		{
			Bits += (Mask & 1u) != 0u ? 1 : 0;
			Mask >>= 1;
		}
		return Bits;
	}

	uint8 ExtractMaskedChannel(uint32 Pixel, uint32 Mask, uint8 DefaultValue)
	{
		if (Mask == 0)
		{
			return DefaultValue;
		}

		const int32 Bits = MaskBits(Mask);
		const uint32 Value = (Pixel & Mask) >> MaskShift(Mask);
		const uint32 MaxValue = Bits >= 32 ? MAX_uint32 : (1u << Bits) - 1u;
		return MaxValue > 0 ? static_cast<uint8>((Value * 255u) / MaxValue) : DefaultValue;
	}

	void DecodeBC1ColorBlock(const uint8* Block, int32 Width, int32 Height, int32 BlockX, int32 BlockY, TArray<uint8>& OutBGRA, const uint8* OverrideAlpha = nullptr)
	{
		FColor Colors[4];
		const uint16 Color0 = ReadU16(Block);
		const uint16 Color1 = ReadU16(Block + 2);
		Colors[0] = ColorFrom565(Color0);
		Colors[1] = ColorFrom565(Color1);

		if (Color0 > Color1 || OverrideAlpha)
		{
			Colors[2] = FColor(
				static_cast<uint8>((2 * Colors[0].R + Colors[1].R) / 3),
				static_cast<uint8>((2 * Colors[0].G + Colors[1].G) / 3),
				static_cast<uint8>((2 * Colors[0].B + Colors[1].B) / 3),
				255);
			Colors[3] = FColor(
				static_cast<uint8>((Colors[0].R + 2 * Colors[1].R) / 3),
				static_cast<uint8>((Colors[0].G + 2 * Colors[1].G) / 3),
				static_cast<uint8>((Colors[0].B + 2 * Colors[1].B) / 3),
				255);
		}
		else
		{
			Colors[2] = FColor(
				static_cast<uint8>((Colors[0].R + Colors[1].R) / 2),
				static_cast<uint8>((Colors[0].G + Colors[1].G) / 2),
				static_cast<uint8>((Colors[0].B + Colors[1].B) / 2),
				255);
			Colors[3] = FColor(0, 0, 0, 0);
		}

		uint32 Indices = static_cast<uint32>(Block[4]) |
			(static_cast<uint32>(Block[5]) << 8) |
			(static_cast<uint32>(Block[6]) << 16) |
			(static_cast<uint32>(Block[7]) << 24);

		for (int32 LocalY = 0; LocalY < 4; ++LocalY)
		{
			for (int32 LocalX = 0; LocalX < 4; ++LocalX)
			{
				FColor Color = Colors[Indices & 0x3];
				if (OverrideAlpha)
				{
					Color.A = OverrideAlpha[LocalY * 4 + LocalX];
				}
				WritePixel(OutBGRA, Width, Height, BlockX * 4 + LocalX, BlockY * 4 + LocalY, Color);
				Indices >>= 2;
			}
		}
	}

	void DecodeBC3AlphaBlock(const uint8* Block, uint8 OutAlpha[16])
	{
		uint8 AlphaValues[8];
		AlphaValues[0] = Block[0];
		AlphaValues[1] = Block[1];
		if (AlphaValues[0] > AlphaValues[1])
		{
			for (int32 Index = 1; Index <= 6; ++Index)
			{
				AlphaValues[Index + 1] = static_cast<uint8>(((7 - Index) * AlphaValues[0] + Index * AlphaValues[1]) / 7);
			}
		}
		else
		{
			for (int32 Index = 1; Index <= 4; ++Index)
			{
				AlphaValues[Index + 1] = static_cast<uint8>(((5 - Index) * AlphaValues[0] + Index * AlphaValues[1]) / 5);
			}
			AlphaValues[6] = 0;
			AlphaValues[7] = 255;
		}

		uint64 Bits = 0;
		for (int32 Index = 0; Index < 6; ++Index)
		{
			Bits |= static_cast<uint64>(Block[2 + Index]) << (8 * Index);
		}

		for (int32 Index = 0; Index < 16; ++Index)
		{
			OutAlpha[Index] = AlphaValues[Bits & 0x7];
			Bits >>= 3;
		}
	}

	void DecodeBC2AlphaBlock(const uint8* Block, uint8 OutAlpha[16])
	{
		for (int32 Index = 0; Index < 16; ++Index)
		{
			const uint8 Packed = Block[Index / 2];
			const uint8 Alpha4 = (Index % 2) == 0 ? (Packed & 0x0f) : (Packed >> 4);
			OutAlpha[Index] = static_cast<uint8>((Alpha4 << 4) | Alpha4);
		}
	}

	void DecodeBC4Block(const uint8* Block, int32 Width, int32 Height, int32 BlockX, int32 BlockY, TArray<uint8>& OutBGRA, bool bGreenChannel = false)
	{
		uint8 Values[16];
		DecodeBC3AlphaBlock(Block, Values);
		for (int32 LocalY = 0; LocalY < 4; ++LocalY)
		{
			for (int32 LocalX = 0; LocalX < 4; ++LocalX)
			{
				const uint8 Value = Values[LocalY * 4 + LocalX];
				FColor Color = bGreenChannel ? FColor(0, Value, 255, 255) : FColor(Value, Value, Value, 255);
				WritePixel(OutBGRA, Width, Height, BlockX * 4 + LocalX, BlockY * 4 + LocalY, Color);
			}
		}
	}

	void DecodeBC5Block(const uint8* Block, int32 Width, int32 Height, int32 BlockX, int32 BlockY, TArray<uint8>& OutBGRA)
	{
		uint8 RedValues[16];
		uint8 GreenValues[16];
		DecodeBC3AlphaBlock(Block, RedValues);
		DecodeBC3AlphaBlock(Block + 8, GreenValues);
		for (int32 LocalY = 0; LocalY < 4; ++LocalY)
		{
			for (int32 LocalX = 0; LocalX < 4; ++LocalX)
			{
				const int32 Index = LocalY * 4 + LocalX;
				WritePixel(OutBGRA, Width, Height, BlockX * 4 + LocalX, BlockY * 4 + LocalY, FColor(RedValues[Index], GreenValues[Index], 255, 255));
			}
		}
	}

	void UnpremultiplyBGRA(TArray<uint8>& BGRA)
	{
		for (int32 Offset = 0; Offset + 3 < BGRA.Num(); Offset += 4)
		{
			const uint32 Alpha = BGRA[Offset + 3];
			if (Alpha == 0)
			{
				BGRA[Offset + 0] = 0;
				BGRA[Offset + 1] = 0;
				BGRA[Offset + 2] = 0;
				continue;
			}

			if (Alpha < 255)
			{
				BGRA[Offset + 0] = static_cast<uint8>(FMath::Min(255u, (static_cast<uint32>(BGRA[Offset + 0]) * 255u + Alpha / 2u) / Alpha));
				BGRA[Offset + 1] = static_cast<uint8>(FMath::Min(255u, (static_cast<uint32>(BGRA[Offset + 1]) * 255u + Alpha / 2u) / Alpha));
				BGRA[Offset + 2] = static_cast<uint8>(FMath::Min(255u, (static_cast<uint32>(BGRA[Offset + 2]) * 255u + Alpha / 2u) / Alpha));
			}
		}
	}

	bool DecodeImageByContent(const FString& FilePath, TArray<uint8>& OutBGRA, int32& OutWidth, int32& OutHeight, FString& OutError)
	{
		FImage Image;
		if (!FImageUtils::LoadImage(*FilePath, Image))
		{
			OutError = TEXT("File is neither a valid DDS nor another supported image format.");
			return false;
		}

		Image.ChangeFormat(ERawImageFormat::BGRA8, EGammaSpace::sRGB);
		if (Image.SizeX <= 0 || Image.SizeY <= 0 || Image.RawData.Num() > MAX_int32)
		{
			OutError = TEXT("Decoded image has an invalid size.");
			return false;
		}

		OutWidth = Image.SizeX;
		OutHeight = Image.SizeY;
		OutBGRA.SetNumUninitialized(static_cast<int32>(Image.RawData.Num()));
		FMemory::Memcpy(OutBGRA.GetData(), Image.RawData.GetData(), Image.RawData.Num());
		return true;
	}

	bool DecodeDDSBaseMip(const FString& FilePath, TArray<uint8>& OutBGRA, int32& OutWidth, int32& OutHeight, FString& OutError)
	{
		TArray<uint8> Data;
		if (!FFileHelper::LoadFileToArray(Data, *FilePath))
		{
			OutError = TEXT("Could not read DDS file.");
			return false;
		}

		if (Data.Num() < 128 || ReadU32(Data, 0) != MakeFourCC('D', 'D', 'S', ' '))
		{
			return DecodeImageByContent(FilePath, OutBGRA, OutWidth, OutHeight, OutError);
		}

		OutHeight = static_cast<int32>(ReadU32(Data, 12));
		OutWidth = static_cast<int32>(ReadU32(Data, 16));
		const uint32 FourCC = ReadU32(Data, 84);
		const uint32 RGBBitCount = ReadU32(Data, 88);
		const uint32 RMask = ReadU32(Data, 92);
		const uint32 GMask = ReadU32(Data, 96);
		const uint32 BMask = ReadU32(Data, 100);
		const uint32 AMask = ReadU32(Data, 104);
		int32 DataOffset = 128;
		uint32 DxgiFormat = 0;

		if (FourCC == MakeFourCC('D', 'X', '1', '0'))
		{
			if (Data.Num() < 148)
			{
				OutError = TEXT("Invalid DDS DX10 header.");
				return false;
			}
			DxgiFormat = ReadU32(Data, 128);
			DataOffset = 148;
		}

		if (OutWidth <= 0 || OutHeight <= 0 || !Data.IsValidIndex(DataOffset))
		{
			OutError = TEXT("Invalid DDS size.");
			return false;
		}

		OutBGRA.SetNumZeroed(OutWidth * OutHeight * 4);

		if (FourCC == 21 || FourCC == 22 || FourCC == 23 || FourCC == 25 || FourCC == 28)
		{
			const int32 LegacyBits = (FourCC == 21 || FourCC == 22) ? 32 : (FourCC == 28 ? 8 : 16);
			const int32 BytesPerPixel = LegacyBits / 8;
			const int32 RequiredBytes = DataOffset + OutWidth * OutHeight * BytesPerPixel;
			if (OutWidth <= 0 || OutHeight <= 0 || Data.Num() < RequiredBytes)
			{
				OutError = TEXT("DDS legacy payload is truncated.");
				return false;
			}

			const uint8* Pixels = Data.GetData() + DataOffset;
			for (int32 Y = 0; Y < OutHeight; ++Y)
			{
				for (int32 X = 0; X < OutWidth; ++X)
				{
					const uint8* Source = Pixels + (Y * OutWidth + X) * BytesPerPixel;
					FColor Color;
					if (FourCC == 21 || FourCC == 22)
					{
						Color = FColor(Source[2], Source[1], Source[0], FourCC == 22 ? 255 : Source[3]);
					}
					else if (FourCC == 23)
					{
						Color = ColorFrom565(ReadU16(Source));
					}
					else if (FourCC == 25)
					{
						const uint16 Value = ReadU16(Source);
						Color.B = static_cast<uint8>(((Value >> 0) & 0x1f) * 255 / 31);
						Color.G = static_cast<uint8>(((Value >> 5) & 0x1f) * 255 / 31);
						Color.R = static_cast<uint8>(((Value >> 10) & 0x1f) * 255 / 31);
						Color.A = (Value & 0x8000) ? 255 : 0;
					}
					else
					{
						Color = FColor(255, 255, 255, Source[0]);
					}
					WritePixel(OutBGRA, OutWidth, OutHeight, X, Y, Color);
				}
			}
			return true;
		}

		const bool bBC1 = FourCC == MakeFourCC('D', 'X', 'T', '1') || DxgiFormat == 71 || DxgiFormat == 72;
		const bool bPremultipliedBC2 = FourCC == MakeFourCC('D', 'X', 'T', '2');
		const bool bPremultipliedBC3 = FourCC == MakeFourCC('D', 'X', 'T', '4');
		const bool bBC2 = bPremultipliedBC2 || FourCC == MakeFourCC('D', 'X', 'T', '3') || DxgiFormat == 74 || DxgiFormat == 75;
		const bool bBC3 = bPremultipliedBC3 || FourCC == MakeFourCC('D', 'X', 'T', '5') || DxgiFormat == 77 || DxgiFormat == 78;
		const bool bBC4 = FourCC == MakeFourCC('A', 'T', 'I', '1') || FourCC == MakeFourCC('B', 'C', '4', 'U') || DxgiFormat == 80 || DxgiFormat == 81;
		const bool bBC5 = FourCC == MakeFourCC('A', 'T', 'I', '2') || FourCC == MakeFourCC('B', 'C', '5', 'U') || DxgiFormat == 83 || DxgiFormat == 84;
		const bool bB5G5R5A1 = DxgiFormat == 86 || (RGBBitCount == 16 && RMask == 0x7c00 && GMask == 0x03e0 && BMask == 0x001f && AMask == 0x8000);

		if (bBC1 || bBC2 || bBC3 || bBC4 || bBC5)
		{
			const int32 BlockBytes = (bBC1 || bBC4) ? 8 : 16;
			const int32 BlocksX = (OutWidth + 3) / 4;
			const int32 BlocksY = (OutHeight + 3) / 4;
			const int32 RequiredBytes = DataOffset + BlocksX * BlocksY * BlockBytes;
			if (Data.Num() < RequiredBytes)
			{
				OutError = TEXT("DDS compressed payload is truncated.");
				return false;
			}

			const uint8* Blocks = Data.GetData() + DataOffset;
			for (int32 BlockY = 0; BlockY < BlocksY; ++BlockY)
			{
				for (int32 BlockX = 0; BlockX < BlocksX; ++BlockX)
				{
					const uint8* Block = Blocks + (BlockY * BlocksX + BlockX) * BlockBytes;
					if (bBC1)
					{
						DecodeBC1ColorBlock(Block, OutWidth, OutHeight, BlockX, BlockY, OutBGRA);
					}
					else if (bBC4)
					{
						DecodeBC4Block(Block, OutWidth, OutHeight, BlockX, BlockY, OutBGRA);
					}
					else if (bBC5)
					{
						DecodeBC5Block(Block, OutWidth, OutHeight, BlockX, BlockY, OutBGRA);
					}
					else
					{
						uint8 Alpha[16];
						if (bBC2)
						{
							DecodeBC2AlphaBlock(Block, Alpha);
						}
						else
						{
							DecodeBC3AlphaBlock(Block, Alpha);
						}
						DecodeBC1ColorBlock(Block + 8, OutWidth, OutHeight, BlockX, BlockY, OutBGRA, Alpha);
					}
				}
			}

			if (bPremultipliedBC2 || bPremultipliedBC3)
			{
				UnpremultiplyBGRA(OutBGRA);
			}
			return true;
		}

		if (DxgiFormat == 28 || DxgiFormat == 29 || DxgiFormat == 87 || DxgiFormat == 88)
		{
			const int32 RequiredBytes = DataOffset + OutWidth * OutHeight * 4;
			if (Data.Num() < RequiredBytes)
			{
				OutError = TEXT("DDS DX10 raw payload is truncated.");
				return false;
			}

			const bool bRGBA = DxgiFormat == 28 || DxgiFormat == 29;
			const uint8* Pixels = Data.GetData() + DataOffset;
			for (int32 Y = 0; Y < OutHeight; ++Y)
			{
				for (int32 X = 0; X < OutWidth; ++X)
				{
					const uint8* Source = Pixels + (Y * OutWidth + X) * 4;
					const FColor Color = bRGBA
						? FColor(Source[0], Source[1], Source[2], Source[3])
						: FColor(Source[2], Source[1], Source[0], DxgiFormat == 88 ? 255 : Source[3]);
					WritePixel(OutBGRA, OutWidth, OutHeight, X, Y, Color);
				}
			}
			return true;
		}

		if (DxgiFormat == 61 || DxgiFormat == 62 || DxgiFormat == 65)
		{
			const int32 RequiredBytes = DataOffset + OutWidth * OutHeight;
			if (Data.Num() < RequiredBytes)
			{
				OutError = TEXT("DDS DX10 8-bit payload is truncated.");
				return false;
			}

			const uint8* Pixels = Data.GetData() + DataOffset;
			for (int32 Y = 0; Y < OutHeight; ++Y)
			{
				for (int32 X = 0; X < OutWidth; ++X)
				{
					const uint8 Value = Pixels[Y * OutWidth + X];
					const FColor Color = DxgiFormat == 65 ? FColor(255, 255, 255, Value) : FColor(Value, Value, Value, 255);
					WritePixel(OutBGRA, OutWidth, OutHeight, X, Y, Color);
				}
			}
			return true;
		}

		if (bB5G5R5A1)
		{
			const int32 RequiredBytes = DataOffset + OutWidth * OutHeight * 2;
			if (Data.Num() < RequiredBytes)
			{
				OutError = TEXT("DDS B5G5R5A1 payload is truncated.");
				return false;
			}

			const uint8* Pixels = Data.GetData() + DataOffset;
			for (int32 Y = 0; Y < OutHeight; ++Y)
			{
				for (int32 X = 0; X < OutWidth; ++X)
				{
					const uint16 Value = ReadU16(Pixels + (Y * OutWidth + X) * 2);
					FColor Color;
					Color.B = static_cast<uint8>(((Value >> 0) & 0x1f) * 255 / 31);
					Color.G = static_cast<uint8>(((Value >> 5) & 0x1f) * 255 / 31);
					Color.R = static_cast<uint8>(((Value >> 10) & 0x1f) * 255 / 31);
					Color.A = (Value & 0x8000) ? 255 : 0;
					WritePixel(OutBGRA, OutWidth, OutHeight, X, Y, Color);
				}
			}
			return true;
		}

		if (FourCC == 0 && (RGBBitCount == 8 || RGBBitCount == 16 || RGBBitCount == 24 || RGBBitCount == 32))
		{
			const int32 BytesPerPixel = static_cast<int32>(RGBBitCount / 8);
			const int32 RequiredBytes = DataOffset + OutWidth * OutHeight * BytesPerPixel;
			if (Data.Num() < RequiredBytes)
			{
				OutError = TEXT("DDS raw payload is truncated.");
				return false;
			}

			const uint8* Pixels = Data.GetData() + DataOffset;
			for (int32 Y = 0; Y < OutHeight; ++Y)
			{
				for (int32 X = 0; X < OutWidth; ++X)
				{
					const uint8* Source = Pixels + (Y * OutWidth + X) * BytesPerPixel;
					uint32 Pixel = 0;
					for (int32 ByteIndex = 0; ByteIndex < BytesPerPixel; ++ByteIndex)
					{
						Pixel |= static_cast<uint32>(Source[ByteIndex]) << (ByteIndex * 8);
					}

					FColor Color;
					if (RGBBitCount == 8 && RMask == 0 && GMask == 0 && BMask == 0)
					{
						Color = FColor(Pixel & 0xff, Pixel & 0xff, Pixel & 0xff, 255);
					}
					else
					{
						Color.R = ExtractMaskedChannel(Pixel, RMask, 0);
						Color.G = ExtractMaskedChannel(Pixel, GMask, 0);
						Color.B = ExtractMaskedChannel(Pixel, BMask, 0);
						Color.A = ExtractMaskedChannel(Pixel, AMask, 255);
					}
					WritePixel(OutBGRA, OutWidth, OutHeight, X, Y, Color);
				}
			}
			return true;
		}

		OutError = FString::Printf(TEXT("Unsupported DDS format. FourCC=%u DXGI=%u RGBBits=%u"), FourCC, DxgiFormat, RGBBitCount);
		return false;
	}

	bool ImportDDSAsTexture2D(const FMT2ImportContext& Context, const FMT2AssetRecord& Record, FString& OutError)
	{
		TArray<uint8> BGRA;
		int32 Width = 0;
		int32 Height = 0;
		if (!DecodeDDSBaseMip(Record.AbsolutePath, BGRA, Width, Height, OutError))
		{
			return false;
		}

		const FString ObjectPath = FMT2AssetScanner::BuildContentObjectPath(Context.DestinationRoot, Record);
		const FString PackageName = FPackageName::ObjectPathToPackageName(ObjectPath);
		const FString AssetName = FPackageName::ObjectPathToObjectName(ObjectPath);

		const FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
		if (!Context.bReplaceExisting && AssetRegistryModule.Get().GetAssetByObjectPath(FSoftObjectPath(ObjectPath)).IsValid())
		{
			return true;
		}

		UPackage* Package = CreatePackage(*PackageName);
		UTexture2D* Texture = NewObject<UTexture2D>(Package, *AssetName, RF_Public | RF_Standalone | RF_Transactional);
		if (!Texture)
		{
			OutError = TEXT("Failed to create Texture2D asset.");
			return false;
		}

		Texture->Modify();
		Texture->Source.Init(Width, Height, 1, 1, TSF_BGRA8, BGRA.GetData());
		Texture->SRGB = true;
		Texture->CompressionSettings = TC_Default;
		FMT2AssetOptimizer::ConfigureTexture(Texture);

		FAssetRegistryModule::AssetCreated(Texture);
		Package->MarkPackageDirty();
		return true;
	}
}

FMT2TextureImporter::FMT2TextureImporter()
	: FMT2ImporterBase(EMT2ImportDomain::Textures, TEXT("TextureImporter"))
{
}

bool FMT2TextureImporter::Discover(const FMT2ImportContext& Context, const FMT2AssetScanResult& ScanResult, FMT2ImportDiscovery& OutDiscovery) const
{
	OutDiscovery.Domain = GetDomain();
	OutDiscovery.AssetRecords = ScanResult.GetRecordsByKind(EMT2AssetKind::Texture);
	OutDiscovery.ItemsDiscovered = OutDiscovery.AssetRecords.Num();
	for (const FMT2AssetRecord& Record : OutDiscovery.AssetRecords)
	{
		OutDiscovery.EntryNames.Add(Record.VirtualPath.IsEmpty() ? Record.ContentPath : Record.VirtualPath);
	}
	return true;
}

bool FMT2TextureImporter::Import(const FMT2ImportRequest& Request, FMT2ImportResult& OutResult)
{
	if (!CanImport(Request, OutResult))
	{
		return false;
	}

	if (Request.Selection.AssetRecords.Num() == 0)
	{
		OutResult.AddWarning(TEXT("No texture records were selected."));
		OutResult.bSucceeded = true;
		return true;
	}

	TArray<UAssetImportTask*> Tasks;
	const int32 MaxItems = Request.MaxItems > 0 ? Request.MaxItems : Request.Selection.AssetRecords.Num();
	int32 ConsideredCount = 0;
	int32 DirectDDSImportedCount = 0;
	FScopedSlowTask TextureProgress(static_cast<float>(MaxItems + 1), NSLOCTEXT("FMT2TextureImporter", "ImportTexturesProgress", "Importing textures..."));

	for (const FMT2AssetRecord& Texture : Request.Selection.AssetRecords)
	{
		if (ConsideredCount >= MaxItems)
		{
			break;
		}

		TextureProgress.EnterProgressFrame(1.0f, FText::Format(
			NSLOCTEXT("FMT2TextureImporter", "ImportTextureProgressFormat", "Checking texture {0} of {1}: {2}"),
			FText::AsNumber(ConsideredCount + 1),
			FText::AsNumber(MaxItems),
			FText::FromString(Texture.VirtualPath)));
		if (Request.Context.IsStopRequested() || TextureProgress.ShouldCancel())
		{
			OutResult.AddWarning(TEXT("Texture import stopped by user."));
			break;
		}

		ConsideredCount++;
		OutResult.ItemsDiscovered++;

		if (Texture.Kind != EMT2AssetKind::Texture)
		{
			OutResult.ItemsSkipped++;
			OutResult.AddWarning(FString::Printf(TEXT("Skipped non-texture record: %s"), *Texture.VirtualPath), Texture.AbsolutePath);
			continue;
		}

		if (!IFileManager::Get().FileExists(*Texture.AbsolutePath))
		{
			OutResult.ItemsSkipped++;
			OutResult.AddWarning(FString::Printf(TEXT("Skipped missing texture file: %s"), *Texture.AbsolutePath), Texture.AbsolutePath);
			continue;
		}

		const FString ObjectPath = BuildObjectPath(Request.Context, Texture);
		const FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
		if (!Request.Context.bReplaceExisting && AssetRegistryModule.Get().GetAssetByObjectPath(FSoftObjectPath(ObjectPath)).IsValid())
		{
			OutResult.ItemsSkipped++;
			OutResult.AddInfo(FString::Printf(TEXT("Skipped already imported texture: %s"), *ObjectPath), Texture.AbsolutePath);
			continue;
		}
		OutResult.CreatedPackages.Add(ObjectPath);
		if (Request.Context.bEnableDebugLogs)
		{
			OutResult.AddInfo(FString::Printf(TEXT("[Debug][TextureImport] Source='%s' Virtual='%s' Content='%s' Dest='%s' Object='%s' Ext='%s'"),
				*Texture.AbsolutePath,
				*Texture.VirtualPath,
				*Texture.ContentPath,
				*BuildDestinationPath(Request.Context, Texture),
				*ObjectPath,
				*Texture.Extension),
				Texture.AbsolutePath);
		}

		if (Request.Context.bDryRun)
		{
			OutResult.ItemsSkipped++;
			continue;
		}

		if (Texture.Extension.Equals(TEXT("dds"), ESearchCase::IgnoreCase))
		{
			FString Error;
			if (ImportDDSAsTexture2D(Request.Context, Texture, Error))
			{
				DirectDDSImportedCount++;
				if (Request.Context.bEnableDebugLogs)
				{
					OutResult.AddInfo(FString::Printf(TEXT("[Debug][TextureImport] DDS direct import succeeded: %s"), *ObjectPath), Texture.AbsolutePath);
				}
			}
			else
			{
				OutResult.ItemsSkipped++;
				OutResult.AddWarning(FString::Printf(TEXT("DDS fallback import failed: %s"), *Error), Texture.AbsolutePath);
			}
			continue;
		}

		UAssetImportTask* Task = NewObject<UAssetImportTask>();
		Task->AddToRoot();
		Task->Filename = Texture.AbsolutePath;
		Task->DestinationPath = BuildDestinationPath(Request.Context, Texture);
		Task->DestinationName = FPackageName::ObjectPathToObjectName(ObjectPath);
		Task->bAutomated = true;
		Task->bSave = false;
		Task->bReplaceExisting = Request.Context.bReplaceExisting;
		Task->Factory = nullptr;
		Tasks.Add(Task);
	}

	if (Tasks.Num() > 0)
	{
		TextureProgress.EnterProgressFrame(1.0f, FText::Format(
			NSLOCTEXT("FMT2TextureImporter", "CreateTextureAssetsProgressFormat", "Creating {0} texture asset(s)..."),
			FText::AsNumber(Tasks.Num())));
		FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
		AssetToolsModule.Get().ImportAssetTasks(Tasks);

		for (UAssetImportTask* Task : Tasks)
		{
			for (UObject* ImportedObject : Task->GetObjects())
			{
				FMT2AssetOptimizer::ConfigureTexture(Cast<UTexture2D>(ImportedObject));
			}
			Task->RemoveFromRoot();
		}
	}

	OutResult.ItemsImported = Tasks.Num() + DirectDDSImportedCount;
	if (Request.Context.bDryRun)
	{
		OutResult.AddInfo(FString::Printf(TEXT("Dry run: prepared %d texture import task(s)."), OutResult.CreatedPackages.Num()));
	}
	else
	{
		OutResult.AddInfo(FString::Printf(TEXT("Imported %d DDS texture(s) with fallback and submitted %d regular texture import task(s)."), DirectDDSImportedCount, Tasks.Num()));
	}

	OutResult.bSucceeded = !OutResult.HasErrors();
	return OutResult.bSucceeded;
}

FString FMT2TextureImporter::BuildDestinationPath(const FMT2ImportContext& Context, const FMT2AssetRecord& Record)
{
	return FMT2AssetScanner::BuildContentPackagePath(Context.DestinationRoot, Record);
}

FString FMT2TextureImporter::BuildObjectPath(const FMT2ImportContext& Context, const FMT2AssetRecord& Record)
{
	return FMT2AssetScanner::BuildContentObjectPath(Context.DestinationRoot, Record);
}

FString FMT2TextureImporter::NormalizeReferencePath(const FString& ReferencePath)
{
	FString Path = ReferencePath;
	Path.TrimStartAndEndInline();
	Path.RemoveFromStart(TEXT("\""));
	Path.RemoveFromEnd(TEXT("\""));
	Path.ReplaceInline(TEXT("\\"), TEXT("/"));
	Path.ReplaceInline(TEXT("//"), TEXT("/"));
	if (Path.StartsWith(TEXT("ymir work/"), ESearchCase::IgnoreCase))
	{
		Path = FString(TEXT("d:/")) + Path;
	}
	return Path;
}

namespace
{
	const FMT2AssetRecord* FindExactTextureRecordForNormalizedReference(const FMT2AssetScanResult& ScanResult, const FString& NormalizedReference)
	{
		for (const FMT2AssetRecord& Record : ScanResult.Records)
		{
			if (Record.Kind != EMT2AssetKind::Texture)
			{
				continue;
			}

			const FString VirtualPath = Record.VirtualPath.ToLower();
			const FString ContentPath = Record.ContentPath.ToLower();
			if (VirtualPath == NormalizedReference ||
				ContentPath == NormalizedReference ||
				NormalizedReference.EndsWith(VirtualPath) ||
				NormalizedReference.EndsWith(ContentPath))
			{
				return &Record;
			}
		}

		return nullptr;
	}

	const FMT2AssetRecord* FindTextureRecordByCleanFilename(const FMT2AssetScanResult& ScanResult, const FString& NormalizedReference)
	{
		const FString CleanReferenceName = FPaths::GetCleanFilename(NormalizedReference);
		for (const FMT2AssetRecord& Record : ScanResult.Records)
		{
			if (Record.Kind == EMT2AssetKind::Texture && FPaths::GetCleanFilename(Record.ContentPath).ToLower() == CleanReferenceName)
			{
				return &Record;
			}
		}
		return nullptr;
	}

	FString BuildLocalTextureReference(const FMT2AssetRecord& ReferencingRecord, const FString& ReferencePath)
	{
		const FString NormalizedReference = FMT2TextureImporter::NormalizeReferencePath(ReferencePath);
		if (NormalizedReference.IsEmpty() ||
			NormalizedReference.StartsWith(TEXT("/")) ||
			(NormalizedReference.Len() > 1 && NormalizedReference[1] == TCHAR(':')))
		{
			return FString();
		}

		FString BasePath = FPaths::GetPath(ReferencingRecord.ContentPath);
		if (BasePath.IsEmpty())
		{
			BasePath = FPaths::GetPath(ReferencingRecord.VirtualPath);
		}
		return FMT2TextureImporter::NormalizeReferencePath(BasePath / NormalizedReference);
	}
}

const FMT2AssetRecord* FMT2TextureImporter::FindRecordForReference(const FMT2AssetScanResult& ScanResult, const FString& ReferencePath)
{
	const FString NormalizedReference = NormalizeReferencePath(ReferencePath).ToLower();
	if (const FMT2AssetRecord* ExactRecord = FindExactTextureRecordForNormalizedReference(ScanResult, NormalizedReference))
	{
		return ExactRecord;
	}
	return FindTextureRecordByCleanFilename(ScanResult, NormalizedReference);
}

const FMT2AssetRecord* FMT2TextureImporter::FindRecordForReference(const FMT2AssetScanResult& ScanResult, const FString& ReferencePath, const FMT2AssetRecord& ReferencingRecord)
{
	const FString NormalizedReference = NormalizeReferencePath(ReferencePath).ToLower();
	if (const FMT2AssetRecord* ExactRecord = FindExactTextureRecordForNormalizedReference(ScanResult, NormalizedReference))
	{
		return ExactRecord;
	}

	const FString LocalReference = BuildLocalTextureReference(ReferencingRecord, ReferencePath).ToLower();
	if (!LocalReference.IsEmpty())
	{
		if (const FMT2AssetRecord* LocalRecord = FindExactTextureRecordForNormalizedReference(ScanResult, LocalReference))
		{
			return LocalRecord;
		}
	}

	if (const FMT2AssetRecord* FilenameRecord = FindTextureRecordByCleanFilename(ScanResult, LocalReference.IsEmpty() ? NormalizedReference : LocalReference))
	{
		return FilenameRecord;
	}
	return FindTextureRecordByCleanFilename(ScanResult, NormalizedReference);
}
