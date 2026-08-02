/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Vendored and modified from https://github.com/mozilla/fxc2 (MPL 2.0).
 * Upstream fxc2 only understood a handful of single-dash getopt-style
 * flags (-T, -E, -D, -Vn, -Vi, -Fh, --nologo). This build adds support
 * for the actual fxc syntax DirectXTK's CompileShaders.cmd uses, so this
 * can act as a drop-in `fxc.exe` (via Wine + LegacyShaderCompiler) for
 * that build script on Linux:
 *   - '/'-prefixed flags (getopt only recognizes '-')
 *   - /WX, /Ges, /Zi, /Zpc -> mapped to real D3DCOMPILE_* flags
 *   - /Qstrip_reflect, /Qstrip_debug -> applied via D3DStripShader
 *   - /Fd (separate PDB output) -> via D3DGetBlobPart(..., D3D_BLOB_PDB, ...)
 * See RecipeConditionPatcher's CMakeLists.txt for how this gets built
 * (llvm-mingw) and invoked (Wine, as part of a vcpkg overlay port for
 * directxtk). PDB extraction is currently non-functional (fails with
 * E_FAIL against the bundled d3dcompiler_47.dll) but non-fatal - nothing
 * in DirectXTK's own build or runtime code reads the .pdb back. */

#include <d3dcompiler.h>
#include <d3dcommon.h>
#include <direct.h>
#include <stdio.h>
#include <getopt.h>
#include <string>
#include <wchar.h>


#define D3D_COMPILE_STANDARD_FILE_INCLUDE ((ID3DInclude*)(UINT_PTR)1)
typedef HRESULT(__stdcall *pCompileFromFileg)(LPCWSTR,
					      const D3D_SHADER_MACRO[],
					      ID3DInclude*,
					      LPCSTR,
					      LPCSTR,
					      UINT,
					      UINT,
					      ID3DBlob**,
					      ID3DBlob**);
typedef HRESULT(__stdcall *pStripShaderg)(LPCVOID, SIZE_T, UINT, ID3DBlob**);
typedef HRESULT(__stdcall *pGetBlobPartg)(LPCVOID, SIZE_T, D3D_BLOB_PART, UINT, ID3DBlob**);

void print_usage_arg() {
  // https://msdn.microsoft.com/en-us/library/windows/desktop/bb509709(v=vs.85).aspx
  printf("You have specified an argument that is not handled by fxc2\n");
  printf("This isn't a sign of disaster, odds are it will be very easy to add support for this argument.\n");
  printf("Review the meaning of the argument in the real fxc program, and then add it into fxc2.\n");
  exit(1);
}
void print_usage_missing(const char* arg) {
  printf("fxc2 is missing the %s argument. We expected to receive this, and it's likely things ", arg);
  printf("will not work correctly without it. Review fxc2 and make sure things will work.\n");
  exit(1);
}
void print_usage_toomany() {
  printf("You specified multiple input files. We did not expect to receive this, and aren't prepared to handle ");
  printf("multiple input files. You'll have to edit the source to behave the way you want.\n");
  exit(1);
}

int main(int argc, char* argv[])
{
  // ====================================================================================
  // Process Command Line Arguments

  int verbose = 1;

  char* model = NULL;
  wchar_t* inputFile = NULL;
  char* entryPoint = NULL;
  char* variableName = NULL;
  char* outputFile = NULL;
  char* pdbFile = NULL;
  UINT compileFlags = 0;
  UINT stripFlags = 0;
  int numDefines = 1;
  D3D_SHADER_MACRO* defines = new D3D_SHADER_MACRO[numDefines];
  defines[numDefines-1].Name = NULL;
  defines[numDefines-1].Definition = NULL;

  int i, c;
  static struct option longOptions[] =
  {
    /* These options set a flag. */
    {"nologo", no_argument,       &verbose, 0},
    {0, 0, 0, 0}
  };

  // Real fxc (and scripts built around it, e.g. DirectXTK's
  // CompileShaders.cmd) use '/'-prefixed flags, and several of them
  // (/WX, /Ges, /Zi, /Zpc, /Qstrip_reflect, /Qstrip_debug) are bare
  // words with no argument at all. getopt has no notion of '/' as an
  // option prefix, and no way to model a flag that isn't either a bare
  // letter or a letter+argument, so those would otherwise all land in
  // the same "unhandled argument" bucket regardless of what they mean.
  // Intercept and translate before getopt ever sees argv, rather than
  // trying to bend getopt's own option string to fit a syntax it wasn't
  // built for: recognized bare words get folded directly into the
  // D3DCOMPILE_*/D3DCOMPILER_STRIP_* flag masks and dropped from what
  // reaches getopt; everything else gets its leading '/' normalized to
  // '-' so the existing T/E/D/V/F handling below still works unchanged.
  char** filteredArgv = new char*[argc];
  int filteredArgc = 0;
  filteredArgv[filteredArgc++] = argv[0];
  for (int a = 1; a < argc; a++) {
    char* arg = argv[a];
    if (arg[0] == '/' || arg[0] == '-') {
      const char* name = arg + 1;
      if (strcmp(name, "WX") == 0) { compileFlags |= D3DCOMPILE_WARNINGS_ARE_ERRORS; continue; }
      if (strcmp(name, "Ges") == 0) { compileFlags |= D3DCOMPILE_ENABLE_STRICTNESS; continue; }
      if (strcmp(name, "Zi") == 0) { compileFlags |= D3DCOMPILE_DEBUG; continue; }
      if (strcmp(name, "Zpc") == 0) { compileFlags |= D3DCOMPILE_PACK_MATRIX_COLUMN_MAJOR; continue; }
      if (strcmp(name, "Qstrip_reflect") == 0) { stripFlags |= D3DCOMPILER_STRIP_REFLECTION_DATA; continue; }
      if (strcmp(name, "Qstrip_debug") == 0) { stripFlags |= D3DCOMPILER_STRIP_DEBUG_INFO; continue; }
    }
    if (arg[0] == '/') {
      char* normalized = strdup(arg);
      normalized[0] = '-';
      filteredArgv[filteredArgc++] = normalized;
    } else {
      filteredArgv[filteredArgc++] = arg;
    }
  }

  while (1) {
    D3D_SHADER_MACRO* newDefines;

    int optionIndex = 0;
    c = getopt_long_only (filteredArgc, filteredArgv, "T:E:D:V:F:",
                     longOptions, &optionIndex);

    /* Detect the end of the options. */
    if (c == -1)
      break;

    switch (c)
    {
      case 0:
        //printf ("option -nologo (quiet)\n");
        //Technically, this is any flag we define in longOptions
        break;
      case 'T':
        model = strdup(optarg);
        if(verbose) {
          printf ("option -T (Shader Model/Profile) with arg %s\n", optarg);
        }
        break;
      case 'E':
        entryPoint = strdup(optarg);
        if(verbose) {
          printf ("option -E (Entry Point) with arg %s\n", optarg);
        }
        break;
      case 'D':
        numDefines++;
        //Copy the old array into the new array, but put the new definition at the beginning
        newDefines = new D3D_SHADER_MACRO[numDefines];
        for(i=1; i<numDefines; i++)
          newDefines[i] = defines[i-1];
        delete[] defines;
        defines = newDefines;
        defines[0].Name = strdup(optarg);
        defines[0].Definition = "1";
        if(verbose) {
          printf ("option -D with arg %s\n", optarg);
        }
        break;

      case 'V':
        switch(optarg[0])
        {
          case 'n':
            variableName = strdup(&optarg[1]);
            if(verbose) {
              printf ("option -Vn (Variable Name) with arg %s\n", &optarg[1]);
            }
            break;
          case 'i':
            if(verbose) {
              printf("option -Vi (Output include process details) acknowledged but ignored.\n");
            }
            break;
          default:
            print_usage_arg();
            break;
        }
        break;
      case 'F':
        switch(optarg[0])
        {
          case 'h':
            outputFile = strdup(&optarg[1]);
            if(verbose) {
              printf ("option -Fh (Output File) with arg %s\n", &optarg[1]);
            }
            break;
          case 'd':
            pdbFile = strdup(&optarg[1]);
            if(verbose) {
              printf ("option -Fd (PDB File) with arg %s\n", &optarg[1]);
            }
            break;
          default:
            print_usage_arg();
            break;
        }
        break;

      case '?':
      default:
        print_usage_arg();
        break;
    }
  }

  if (optind < filteredArgc) {
    inputFile = new wchar_t[strlen(filteredArgv[optind])+1];
    mbstowcs(inputFile, filteredArgv[optind], strlen(filteredArgv[optind])+1);
    if(verbose) {
      wprintf(L"input file: %ls\n", inputFile);
    }

    optind++;
    if(optind < filteredArgc) {
      print_usage_toomany();
    }
  }

  if(inputFile == NULL)
    print_usage_missing("inputFile");
  if(model == NULL)
    print_usage_missing("model");
  if(entryPoint == NULL)
    print_usage_missing("entryPoint");
  if(defines == NULL)
    print_usage_missing("defines");
  if(variableName == NULL)
    print_usage_missing("variableName");
  if(outputFile == NULL)
    print_usage_missing("outputFile");
  // pdbFile (/Fd) is deliberately NOT required here - real fxc treats it
  // as optional too, and plenty of real invocations never pass it.

  // ====================================================================================
  // Shader Compilation

  //Find the WINDOWS dll
  char dllPath[ MAX_PATH ];
  int bytes = GetModuleFileName(NULL, dllPath, MAX_PATH);
  if(bytes == 0) {
    printf("Could not retrieve the directory of the running executable.\n");
    return 1;
  }
  //Fill rest of the buffer with NULLs
  memset(dllPath + bytes, '\0', MAX_PATH - bytes);
  //Copy the dll location over top fxc2.exe
  strcpy(strrchr(dllPath, '\\') + 1, "d3dcompiler_47.dll");

  HMODULE h = LoadLibrary(dllPath);
  if(h == NULL) {
    printf("Error: could not load d3dcompiler_47.dll from %s\n", dllPath);
    return 1;
  }

  pCompileFromFileg ptr = (pCompileFromFileg)GetProcAddress(h, "D3DCompileFromFile");
  if(ptr == NULL) {
    printf("Error: could not get the address of D3DCompileFromFile.\n");
    return 1;
  }
  // D3DStripShader/D3DGetBlobPart are only needed for /Qstrip_* and /Fd
  // respectively - both optional, so a missing export from an older
  // d3dcompiler_47.dll degrades to "skip that step" rather than aborting
  // the whole compile (GetProcAddress returning NULL is checked at each
  // call site below, not here).
  pStripShaderg stripPtr = (pStripShaderg)GetProcAddress(h, "D3DStripShader");
  pGetBlobPartg blobPartPtr = (pGetBlobPartg)GetProcAddress(h, "D3DGetBlobPart");

  HRESULT hr;
  ID3DBlob* output = NULL;
  ID3DBlob* errors = NULL;

  if(verbose) {
    printf("Calling D3DCompileFromFile(\n");

    wprintf(L"\t %ls,\n", inputFile);

    printf("\t");
    for(i=0; i<numDefines-1; i++)
      printf(" %s=%s", defines[i].Name, defines[i].Definition);
    printf(",\n");

    printf("\t D3D_COMPILE_STANDARD_FILE_INCLUDE,\n");

    printf("\t %s,\n", entryPoint);

    printf("\t %s,\n", model);

    printf("\t %u,\n", compileFlags);
    printf("\t 0,\n");
    printf("\t &output,\n");
    printf("\t &errors);\n");
  }

  /*
  HRESULT WINAPI D3DCompileFromFile(
  in      LPCWSTR pFileName,
  in_opt  const D3D_SHADER_MACRO pDefines,
  in_opt  ID3DInclude pInclude,
  in      LPCSTR pEntrypoint,
  in      LPCSTR pTarget,
  in      UINT Flags1,
  in      UINT Flags2,
  out     ID3DBlob ppCode,
  out_opt ID3DBlob ppErrorMsgs
  );
  */
  hr = ptr(
    inputFile,
    defines,
    D3D_COMPILE_STANDARD_FILE_INCLUDE,
    entryPoint,
    model,
    compileFlags,
    0,
    &output,
    &errors
    );

  // ====================================================================================
  // Output (or errors)

  if (FAILED(hr)) {
   if (errors) {
    char* error = (char*)errors->GetBufferPointer();
    printf("Got an error (%i) while compiling:\n%s\n", hr, error);
    errors->Release();
   } else {
     printf("Got an error (%i) while compiling, but no error message from the function.\n", hr);

     LPSTR messageBuffer = nullptr;
     size_t size = FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                                 NULL, hr, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&messageBuffer, 0, NULL);
     printf("Windows Error Message: %s\n", messageBuffer);
     LocalFree(messageBuffer);
   }

   if (output)
     output->Release();

   return hr;
  } else {
    // /Fd: extract the debug/PDB part from the UNSTRIPPED blob (stripping
    // removes exactly this data, so it has to happen before any
    // /Qstrip_debug is applied below). Best-effort: nothing in
    // DirectXTK's own build or runtime reads this file back, it exists
    // purely for external debuggers/RenderDoc/PIX, so a failure here
    // (e.g. no debug info because /Zi wasn't set, or an older
    // d3dcompiler_47.dll without this export) is reported but not fatal.
    if (pdbFile != NULL) {
      if (blobPartPtr == NULL) {
        printf("Warning: d3dcompiler_47.dll has no D3DGetBlobPart export; skipping %s\n", pdbFile);
      } else {
        ID3DBlob* pdbBlob = NULL;
        HRESULT pdbHr = blobPartPtr(output->GetBufferPointer(), output->GetBufferSize(), D3D_BLOB_PDB, 0, &pdbBlob);
        if (FAILED(pdbHr) || pdbBlob == NULL) {
          printf("Warning: could not extract PDB blob (%i); skipping %s\n", pdbHr, pdbFile);
        } else {
          FILE* pdbF;
          errno_t pdbErr = fopen_s(&pdbF, pdbFile, "wb");
          if (pdbErr == 0 && pdbF != NULL) {
            fwrite(pdbBlob->GetBufferPointer(), 1, pdbBlob->GetBufferSize(), pdbF);
            fclose(pdbF);
            if (verbose) {
              printf("Wrote %zu bytes of PDB output to %s\n", pdbBlob->GetBufferSize(), pdbFile);
            }
          }
          pdbBlob->Release();
        }
      }
    }

    // /Qstrip_reflect and /Qstrip_debug: strip the requested parts out of
    // the blob that actually becomes the .inc header content. Real fxc
    // does this by default with these flags to keep shipped shader
    // blobs small; functionally the unstripped blob would still work,
    // this is a size optimization, so a missing export or a failed
    // strip call degrades to "ship the unstripped blob" rather than
    // aborting the build.
    ID3DBlob* finalBlob = output;
    ID3DBlob* strippedBlob = NULL;
    if (stripFlags != 0) {
      if (stripPtr == NULL) {
        printf("Warning: d3dcompiler_47.dll has no D3DStripShader export; shipping unstripped output.\n");
      } else {
        HRESULT stripHr = stripPtr(output->GetBufferPointer(), output->GetBufferSize(), stripFlags, &strippedBlob);
        if (FAILED(stripHr) || strippedBlob == NULL) {
          printf("Warning: D3DStripShader failed (%i); shipping unstripped output.\n", stripHr);
        } else {
          finalBlob = strippedBlob;
        }
      }
    }

    char * outString = (char*)finalBlob->GetBufferPointer();
    int len = finalBlob->GetBufferSize();

    FILE* f;
    errno_t err = fopen_s(&f, outputFile, "w");

    // The array name is variableName (/Vn), NOT entryPoint (/E) - real fxc
    // always names its output array after /Vn specifically so callers can
    // give the array a name distinct from the HLSL entry point it came
    // from (e.g. /EPSMonochrome /VnPostProcess_PSMonochrome). Upstream
    // fxc2 used entryPoint here instead, which happens to look right for
    // simple single-shader-per-file cases but produces a wrongly-named
    // symbol wherever a caller (e.g. DirectXTK's build) relies on /Vn to
    // disambiguate multiple shaders sharing an entry point name across
    // different source files.
    fprintf(f, "const signed char %s[] =\n{\n", variableName);
    for (i = 0; i < len; i++) {
     fprintf(f, "%4i", outString[i]);
     if (i != len - 1)
       fprintf(f, ",");
     if (i % 6 == 5)
       fprintf(f, "\n");
    }

    fprintf(f, "\n};\n");
    fclose(f);

    if(verbose) {
      printf("Wrote %i bytes of shader output to %s\n", len, outputFile);
    }

    if (strippedBlob != NULL)
      strippedBlob->Release();
  }

  return 0;
}
