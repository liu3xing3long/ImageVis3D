#ifndef DATASOURCE_H
#define DATASOURCE_H

#include <sstream>
#include <iostream>
#include <cmath>
#include <cstdint>

#include "../Tuvok/Controller/Controller.h"

#include "../Tuvok/StdTuvokDefines.h"
#include "../Tuvok/Controller/Controller.h"
#include "../Tuvok/Basics/Timer.h"
#include "../Tuvok/Basics/Vectors.h"
#include "../Tuvok/Basics/LargeRAWFile.h"
#include "../Tuvok/Basics/SysTools.h"
#include "../CmdLineConverter/DebugOut/HRConsoleOut.h"

#include "../Tuvok/IO/TuvokSizes.h"
#include "../Tuvok/IO/UVF/UVF.h"
#include "../Tuvok/IO/UVF/Histogram1DDataBlock.h"
#include "../Tuvok/IO/UVF/Histogram2DDataBlock.h"
#include "../Tuvok/IO/UVF/MaxMinDataBlock.h"
#include "../Tuvok/IO/UVF/RasterDataBlock.h"
#include "../Tuvok/IO/UVF/TOCBlock.h"
#include "../Tuvok/IO/UVF/KeyValuePairDataBlock.h"

using namespace std;

double radius(double x, double y, double z)
{
  return std::sqrt(x*x + y*y + z*z);
}

double phi(double x, double y)
{
  return std::atan2(y, x);
}

double theta(double x, double y, double z)
{
  return std::atan2(std::sqrt(x*x + y*y), z);
}

double PowerX(double x, double y, double z, double cx, int n, double power)
{
  return cx + power*
              std::sin(theta(x,y,z)*n)*
              std::cos(phi(x,y)*n);
}

double PowerY(double x, double y, double z,
              double cy, int n, double power)
{
  return cy + power*
              std::sin(theta(x,y,z)*n)*
              std::sin(phi(x,y)*n);
}

double PowerZ(double x, double y, double z,
              double cz, int n, double power)
{
  return cz + power*std::cos(theta(x,y,z)*n);
}

double ComputeMandelbulb(const double sx, const double sy,
                         const double sz, const uint32_t n,
                         const uint32_t iMaxIterations, 
                         const double fBailout) {

  double fx = 0;
  double fy = 0;
  double fz = 0;
  double r = radius(fx, fy, fz);

  for (uint32_t i = 0; i < iMaxIterations; i++) {

    const double fPower = std::pow(r, static_cast<double>(n));

    const double fx_ = PowerX(fx, fy, fz, sx, n, fPower);
    const double fy_ = PowerY(fx, fy, fz, sy, n, fPower);
    const double fz_ = PowerZ(fx, fy, fz, sz, n, fPower);

    fx = fx_;
    fy = fy_;
    fz = fz_;

    if ((r = radius(fx, fy, fz)) > fBailout)
      return static_cast<double>(i) / iMaxIterations;
  }

  return 1.0;
}

template<typename T, bool bMandelbulb> 
void GenerateVolumeData(UINT64VECTOR3 vSize, LargeRAWFile_ptr pDummyData) {
  Timer timer;
  T* source = new T[size_t(vSize.x)];
  
  timer.Start();

  uint64_t miliSecsHalfWay=0;

  for (uint64_t z = 0;z<vSize.z;z++) {
    uint64_t miliSecs = uint64_t(timer.Elapsed());

    if (z < vSize.z/2) {
      const uint64_t secs  = (miliSecs/1000)%60;
      const uint64_t mins  = (miliSecs/60000)%60;
      const uint64_t hours = (miliSecs/3600000);
      MESSAGE("Generating Data %.3f%% completed (Elapsed Time %i:%02i:%02i)",
              100.0*(double)z/vSize.z, int(hours), int(mins), int(secs));
    } else 
    if (z > vSize.z/2) {
      miliSecs = std::max<uint64_t>(0, miliSecsHalfWay*2-miliSecs);
      const uint64_t secs  = (miliSecs/1000)%60;
      const uint64_t mins  = (miliSecs/60000)%60;
      const uint64_t hours = (miliSecs/3600000);
      MESSAGE("Generating Data %.3f%% completed (Remaining Time %i:%02i:%02i)", 
               100.0*(double)z/vSize.z, int(hours), int(mins), int(secs));
    }
    else {
      miliSecsHalfWay = miliSecs;
    }

    for (uint64_t y = 0;y<vSize.y;y++) {
      #pragma omp parallel for 
      for (int64_t x = 0;x<int64_t(vSize.x);x++) {
        if (bMandelbulb)
          source[x] = 
            static_cast<T>(ComputeMandelbulb(2.25 * static_cast<double>(x)/
                                                   (vSize.x-1) - 1.125,
                                             2.25 * static_cast<double>(y)/
                                                   (vSize.y-1) - 1.125,
                                             2.25 * static_cast<double>(z)/
                                                   (vSize.z-1) - 1.125,
                                             8, 
                                             std::numeric_limits<T>::max(),
                                             4.0) 
                                               * std::numeric_limits<T>::max());
        else
          source[x] = 
           static_cast<T>(std::max(0.0f,
                                   (0.5f-(0.5f-FLOATVECTOR3(float(x),
                                                            float(y),
                                                            float(z))/
                                              FLOATVECTOR3(vSize)).length())*
                                              std::numeric_limits<T>::max()*2));
      }
      pDummyData->WriteRAW((uint8_t*)source, vSize.x*sizeof(T));
    }
  }

  delete [] source;
}

bool CreateUVFFile(const std::string& strUVFName, const UINT64VECTOR3& vSize, 
                   uint32_t iBitSize, bool bMandelbulb, uint32_t iBrickSize,
                   bool bUseToCBlock, bool bKeepRaw) {  
  wstring wstrUVFName(strUVFName.begin(), strUVFName.end());
  UVF uvfFile(wstrUVFName);

  const bool bGenerateUVF = 
        SysTools::ToLowerCase(SysTools::GetExt(strUVFName)) == "uvf";
  std::string rawFilename =  
        bGenerateUVF ? SysTools::ChangeExt(strUVFName,"raw") : strUVFName;

  MESSAGE("Generating dummy data");

  LargeRAWFile_ptr dummyData = LargeRAWFile_ptr(new LargeRAWFile(rawFilename));
  if (!dummyData->Create(vSize.volume()*iBitSize/8)) {
    T_ERROR("Failed to create %s file.", rawFilename.c_str());
    return false;
  }

  switch (iBitSize) {
    case 8 :
      if (bMandelbulb)
        GenerateVolumeData<uint8_t, true>(vSize, dummyData);
      else
        GenerateVolumeData<uint8_t, false>(vSize, dummyData);
      break;
    case 16 :
      if (bMandelbulb)
        GenerateVolumeData<uint16_t, true>(vSize, dummyData);
      else
        GenerateVolumeData<uint16_t, false>(vSize, dummyData);
      break;
    default:
      T_ERROR("Invalid bitsize");
      return false;
  }
  dummyData->Close();

  if (!bGenerateUVF) return EXIT_SUCCESS;


  MESSAGE("Preparing creation of UVF file %s", strUVFName.c_str());

  GlobalHeader uvfGlobalHeader;
  uvfGlobalHeader.ulChecksumSemanticsEntry = UVFTables::CS_MD5;
  uvfFile.SetGlobalHeader(uvfGlobalHeader);

  std::shared_ptr<DataBlock> testBlock(new DataBlock());
  testBlock->strBlockID = "Test Block 1";
  testBlock->ulCompressionScheme = UVFTables::COS_NONE;
  uvfFile.AddDataBlock(testBlock);

  testBlock = std::shared_ptr<DataBlock>(new DataBlock());
  testBlock->strBlockID = "Test Block 2";
  uvfFile.AddDataBlock(testBlock);

  std::shared_ptr<DataBlock> pTestVolume;
  std::shared_ptr<MaxMinDataBlock> MaxMinData(
    new MaxMinDataBlock(1)
  );
  std::shared_ptr<RasterDataBlock> testRasterVolume(
    new RasterDataBlock()
  );
  std::shared_ptr<TOCBlock> tocBlock(new TOCBlock());

  if (bUseToCBlock)  {
    tocBlock->strBlockID = "Test TOC Volume 1";
    tocBlock->ulCompressionScheme = UVFTables::COS_NONE;

    bool bResult = tocBlock->FlatDataToBrickedLOD(rawFilename,
      "./tempFile.tmp", iBitSize == 8 ? ExtendedOctree::CT_UINT8
                                      : ExtendedOctree::CT_UINT16,
      1, vSize, DOUBLEVECTOR3(1,1,1),
      UINT64VECTOR3(iBrickSize,iBrickSize,iBrickSize),
      DEFAULT_BRICKOVERLAP, false, false,
      1024*1024*1024, MaxMinData,
      &tuvok::Controller::Debug::Out()
    );

    if (!bResult) {
      T_ERROR("Failed to subdivide the volume into bricks");
      dummyData->Delete();
      uvfFile.Close();
      return false;
    }

    pTestVolume = tocBlock;
  } else {
    testRasterVolume->strBlockID = "Test Volume 1";

    testRasterVolume->ulCompressionScheme = UVFTables::COS_NONE;
    testRasterVolume->ulDomainSemantics.push_back(UVFTables::DS_X);
    testRasterVolume->ulDomainSemantics.push_back(UVFTables::DS_Y);
    testRasterVolume->ulDomainSemantics.push_back(UVFTables::DS_Z);

    testRasterVolume->ulDomainSize.push_back(vSize.x);
    testRasterVolume->ulDomainSize.push_back(vSize.y);
    testRasterVolume->ulDomainSize.push_back(vSize.z);

    testRasterVolume->ulLODDecFactor.push_back(2);
    testRasterVolume->ulLODDecFactor.push_back(2);
    testRasterVolume->ulLODDecFactor.push_back(2);

    testRasterVolume->ulLODGroups.push_back(0);
    testRasterVolume->ulLODGroups.push_back(0);
    testRasterVolume->ulLODGroups.push_back(0);

    uint64_t iLodLevelCount = 1;
    uint32_t iMaxVal = uint32_t(vSize.maxVal());

    while (iMaxVal > iBrickSize) {
      iMaxVal /= 2;
      iLodLevelCount++;
    }

    testRasterVolume->ulLODLevelCount.push_back(iLodLevelCount);

    testRasterVolume->SetTypeToScalar(iBitSize,iBitSize,false,UVFTables::ES_CT);

    testRasterVolume->ulBrickSize.push_back(iBrickSize);
    testRasterVolume->ulBrickSize.push_back(iBrickSize);
    testRasterVolume->ulBrickSize.push_back(iBrickSize);

    testRasterVolume->ulBrickOverlap.push_back(DEFAULT_BRICKOVERLAP*2);
    testRasterVolume->ulBrickOverlap.push_back(DEFAULT_BRICKOVERLAP*2);
    testRasterVolume->ulBrickOverlap.push_back(DEFAULT_BRICKOVERLAP*2);

    vector<double> vScale;
    vScale.push_back(double(vSize.maxVal())/double(vSize.x));
    vScale.push_back(double(vSize.maxVal())/double(vSize.y));
    vScale.push_back(double(vSize.maxVal())/double(vSize.z));
    testRasterVolume->SetScaleOnlyTransformation(vScale);

    dummyData->Open();
    switch (iBitSize) {
    case 8 : {
                  if (!testRasterVolume->FlatDataToBrickedLOD(dummyData,
                    "./tempFile.tmp", CombineAverage<unsigned char,1>, 
                    SimpleMaxMin<unsigned char,1>, MaxMinData, 
                    &tuvok::Controller::Debug::Out())){
                    T_ERROR("Failed to subdivide the volume into bricks");
                    uvfFile.Close();
                    dummyData->Delete();
                    return false;
                  }
                  break;
                }
    case 16 :{
                if (!testRasterVolume->FlatDataToBrickedLOD(dummyData, 
                  "./tempFile.tmp", CombineAverage<unsigned short,1>, 
                  SimpleMaxMin<unsigned short,1>, MaxMinData, 
                  &tuvok::Controller::Debug::Out())){
                  T_ERROR("Failed to subdivide the volume into bricks");
                  uvfFile.Close();
                  dummyData->Delete();
                  return false;
                }
                break;
              }
    }

    string strProblemDesc;
    if (!testRasterVolume->Verify(&strProblemDesc)) {
      T_ERROR("Verify failed with the following reason: %s",
              strProblemDesc.c_str());
      uvfFile.Close();
      dummyData->Delete();
      return false;
    }

    pTestVolume = testRasterVolume;
  }
    
  if (!bKeepRaw) dummyData->Delete();

  if (!uvfFile.AddDataBlock(pTestVolume)) {
    T_ERROR("AddDataBlock failed!");
    uvfFile.Close();
    return false;
  }

  std::shared_ptr<Histogram1DDataBlock> Histogram1D(
    new Histogram1DDataBlock()
  );
  std::shared_ptr<Histogram2DDataBlock> Histogram2D(
    new Histogram2DDataBlock()
  );
  if (bUseToCBlock) {
    MESSAGE("Computing 1D Histogram...");
    if (!Histogram1D->Compute(tocBlock.get(), 0)) {
      T_ERROR("Computation of 1D Histogram failed!");
      uvfFile.Close();
      return false;
    }
    Histogram1D->Compress(4096);
    MESSAGE("Computing 2D Histogram...");
    if (!Histogram2D->Compute(tocBlock.get(), 0,
                              Histogram1D->GetHistogram().size(),
                              MaxMinData->GetGlobalValue().maxScalar)) {
      T_ERROR("Computation of 2D Histogram failed!");
      uvfFile.Close();
      return false;
    }
  } else {
    if (!Histogram1D->Compute(testRasterVolume.get())) {
      T_ERROR("Computation of 1D Histogram failed!");
      uvfFile.Close();
      return false;
    }
    Histogram1D->Compress(4096);
    MESSAGE("Computing 2D Histogram...");
    if (!Histogram2D->Compute(testRasterVolume.get(),
                              Histogram1D->GetHistogram().size(),
                              MaxMinData->GetGlobalValue().maxScalar)) {
      T_ERROR("Computation of 2D Histogram failed!");
      uvfFile.Close();
      return false;
    }
  }

  MESSAGE("Storing histogram data...");
  uvfFile.AddDataBlock(Histogram1D);
  uvfFile.AddDataBlock(Histogram2D);

  MESSAGE("Storing acceleration data...");
  uvfFile.AddDataBlock(MaxMinData);

  MESSAGE("Storing metadata...");

  std::shared_ptr<KeyValuePairDataBlock> metaPairs(
    new KeyValuePairDataBlock()
  );
  metaPairs->AddPair("Data Source","This file was created by the UVFReader");
  metaPairs->AddPair("Description","Dummy file for testing purposes.");

  if (EndianConvert::IsLittleEndian())
    metaPairs->AddPair("Source Endianess","little");
  else
    metaPairs->AddPair("Source Endianess","big");

  metaPairs->AddPair("Source Type","integer");
  metaPairs->AddPair("Source Bit width",SysTools::ToString(iBitSize));

  uvfFile.AddDataBlock(metaPairs);

  MESSAGE("Writing UVF file...");

  if (!uvfFile.Create()) {
    T_ERROR("Failed to create UVF file %s", strUVFName.c_str());
    return false;
  }

  MESSAGE("Computing checksum...");
  uvfFile.Close();

  MESSAGE("Successfully created UVF file %s", strUVFName.c_str());
  return true;
}

#endif // DATASOURCE_H

/*
   For more information, please see: http://software.sci.utah.edu

   The MIT License

   Copyright (c) 2012 Interactive Visualization and Data Analysis Group

   Permission is hereby granted, free of charge, to any person obtaining a
   copy of this software and associated documentation files (the "Software"),
   to deal in the Software without restriction, including without limitation
   the rights to use, copy, modify, merge, publish, distribute, sublicense,
   and/or sell copies of the Software, and to permit persons to whom the
   Software is furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be included
   in all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
   OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
   THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
   LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
   FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
   DEALINGS IN THE SOFTWARE.
*/
