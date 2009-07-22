// -*- mode: cpp; mode: fold -*-
// Description								/*{{{*/
// $Id: cdrom.cc,v 1.20.2.1 2004/01/16 18:58:50 mdz Exp $
/* ######################################################################

   CDROM URI method for APT
   
   ##################################################################### */
									/*}}}*/
// Include Files							/*{{{*/
#include <apt-pkg/acquire-method.h>
#include <apt-pkg/cdrom.h>
#include <apt-pkg/cdromutl.h>
#include <apt-pkg/error.h>
#include <apt-pkg/configuration.h>
#include <apt-pkg/fileutl.h>
#include <apt-pkg/hashes.h>

#include <sys/stat.h>
#include <unistd.h>
#include <dlfcn.h>

#include <iostream>
#include <apti18n.h>
									/*}}}*/

using namespace std;

class CDROMMethod : public pkgAcqMethod
{
   bool DatabaseLoaded;
   ::Configuration Database;
   string CurrentID;
   string CDROM;
   bool MountedByApt;
 
   bool IsCorrectCD(URI want, string MountPath);
   virtual bool Fetch(FetchItem *Itm);
   string GetID(string Name);
   virtual void Exit();
      
   public:
   
   CDROMMethod();
};

// CDROMMethod::CDROMethod - Constructor				/*{{{*/
// ---------------------------------------------------------------------
/* */
CDROMMethod::CDROMMethod() : pkgAcqMethod("1.0",SingleInstance | LocalOnly |
					  SendConfig | NeedsCleanup |
					  Removable), 
                                          DatabaseLoaded(false), 
                                          MountedByApt(false)
{


};
									/*}}}*/
// CDROMMethod::Exit - Unmount the disc if necessary			/*{{{*/
// ---------------------------------------------------------------------
/* */
void CDROMMethod::Exit()
{ 
  if (MountedByApt == true)
      UnmountCdrom(CDROM);
}
									/*}}}*/
// CDROMMethod::GetID - Search the database for a matching string	/*{{{*/
// ---------------------------------------------------------------------
/* */
string CDROMMethod::GetID(string Name)
{
   // Search for an ID
   const Configuration::Item *Top = Database.Tree("CD");
   if (Top != 0)
      Top = Top->Child;
   
   for (; Top != 0;)
   {      
      if (Top->Value == Name)
	 return Top->Tag;
      
      Top = Top->Next;
   }
   return string();
}
									/*}}}*/

// CDROMMethod::IsCorrectCD                                             /*{{{*/
// ---------------------------------------------------------------------
/* */
bool CDROMMethod::IsCorrectCD(URI want, string MountPath)
{
   bool Debug = _config->FindB("Debug::Acquire::cdrom",false);
   string NewID;

   for (unsigned int Version = 2; Version != 0; Version--)
   {
      if (IdentCdrom(MountPath,NewID,Version) == false)
	 return false;
      
      if (Debug == true)
	 clog << "ID " << Version << " " << NewID << endl;
      
      // A hit
      if (Database.Find("CD::" + NewID) == want.Host)
	 return true;
   }
   
   return false;
}
									/*}}}*/
// CDROMMethod::Fetch - Fetch a file					/*{{{*/
// ---------------------------------------------------------------------
/* */
bool CDROMMethod::Fetch(FetchItem *Itm)
{
   URI Get = Itm->Uri;
   string File = Get.Path;
   FetchResult Res;

   bool Debug = _config->FindB("Debug::Acquire::cdrom",false);
   if (Debug)
      clog << "CDROMMethod::Fetch " << Itm->Uri << endl;

   /* All IMS queries are returned as a hit, CDROMs are readonly so 
      time stamps never change */
   if (Itm->LastModified != 0)
   {
      Res.LastModified = Itm->LastModified;
      Res.IMSHit = true;
      Res.Filename = Itm->DestFile;
      URIDone(Res);
      return true;
   }

   // Load the database
   if (DatabaseLoaded == false)
   {
      // Read the database
      string DFile = _config->FindFile("Dir::State::cdroms");
      if (FileExists(DFile) == true)
      {
	 if (ReadConfigFile(Database,DFile) == false)
	    return _error->Error(_("Unable to read the cdrom database %s"),
			  DFile.c_str());
      }
      DatabaseLoaded = true;
   }
       
   // All non IMS queries for package files fail.
   if (Itm->IndexFile == true || GetID(Get.Host).empty() == true)
   {
      Fail(_("Please use apt-cdrom to make this CD-ROM recognized by APT."
	   " apt-get update cannot be used to add new CD-ROMs"));
      return true;
   }

   // We already have a CD inserted, but it is the wrong one
   if (CurrentID.empty() == false && 
       CurrentID != "FAIL" &&
       Database.Find("CD::" + CurrentID) != Get.Host)
   {
      Fail(_("Wrong CD-ROM"),true);
      return true;
   }

   CDROM = _config->FindDir("Acquire::cdrom::mount","/cdrom/");
   if (Debug)
      clog << "Looking for CDROM at " << CDROM << endl;

   if (CDROM[0] == '.')
      CDROM= SafeGetCWD() + '/' + CDROM;
   string NewID;
   pkgUdevCdromDevices udev;

   while (CurrentID.empty() == true)
   {
      // hrm, ugly the loop here
      if (CDROM == "apt-udev-auto/") 
      {
	 if(udev.Dlopen())
	 {
	    vector<struct CdromDevice> v = udev.Scan();
	    for (unsigned int i=0; i < v.size(); i++)
	    {
	       if (Debug)
		  clog << "Have cdrom device " << v[i].DeviceName << endl;
	       if (!v[i].Mounted) 
	       {
		  if (!FileExists("/media/apt"))
		     mkdir("/media/apt", 0755);
		  if(MountCdrom("/media/apt", v[i].DeviceName)) 
		  {
		     if (IsCorrectCD(Get, "/media/apt"))
		     {
			MountedByApt = true;
			CDROM = "/media/apt";
			break;
		     } else {
			UnmountCdrom("/media/apt");
		     }
		  }
	       } else {
		  if (IsCorrectCD(Get, v[i].MountPath))
		  {
		     CDROM = v[i].MountPath;
		     break;
		  }
	       }
	    }
	 } else {
	    _error->WarningE("udev.Dlopen() failed","foo");
	 }
      }

      bool Hit = false;
      if(!IsMounted(CDROM))
	 MountedByApt = MountCdrom(CDROM);
      
      if (IsCorrectCD(Get, CDROM))
	 break;
	 
      // I suppose this should prompt somehow?
      if (_config->FindB("APT::CDROM::NoMount",false) == false &&
	  UnmountCdrom(CDROM) == false)
	 return _error->Error(_("Unable to unmount the CD-ROM in %s, it may still be in use."),
			      CDROM.c_str());
      if (MediaFail(Get.Host,CDROM) == false)
      {
	 CurrentID = "FAIL";
	 return _error->Error(_("Disk not found."));
      }
   }
   
   // Found a CD
   Res.Filename = CDROM + File;
   struct stat Buf;
   if (stat(Res.Filename.c_str(),&Buf) != 0)
      return _error->Error(_("File not found"));
   
   if (NewID.empty() == false)
      CurrentID = NewID;
   Res.LastModified = Buf.st_mtime;
   Res.Size = Buf.st_size;

   Hashes Hash;
   FileFd Fd(Res.Filename, FileFd::ReadOnly);
   Hash.AddFD(Fd.Fd(), Fd.Size());
   Res.TakeHashes(Hash);

   URIDone(Res);
   return true;
}
									/*}}}*/

int main()
{
   setlocale(LC_ALL, "");

   CDROMMethod Mth;
   return Mth.Run();
}
