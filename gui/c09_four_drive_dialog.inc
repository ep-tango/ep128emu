  // Rebuild after each media operation; no VM lock during FLTK events.
  // This is an event-pumping modeless view: other emulator menus continue
  // receiving input. Re-entrant Alt+D is guarded by the enclosing callback.
  bool focusAfterControllerDisable=false;
  for (;;) {
    // A restart request terminates the previous VM instance only once this
    // callback unwinds; never rebuild a ZX panel while a machine switch waits.
    if(g.restartMachineType>=0 || !g.mainWindow->shown() ||
       typeid(g.vm)!=typeid(ZX128::ZX128VM))return;
    bool speccy=false,beta=false,mounted[4]={false,false,false,false};
    bool protectedMedia[4]={false,false,false,false};
    bool canProtect[4]={true,true,true,true};
    unsigned selectedDrive=0;
    std::string paths[4];
    if(!g.lockVMThread(100)) {
      g.errorMessage("Spectrum disk manager: VM busy; retry");return;
    }
    try {
      ZX128::ZX128VM& zx=static_cast<ZX128::ZX128VM&>(g.vm);
      ZX128::SpeccyDOSBus *sp=zx.getSpeccyDOSForTesting();
      ZX128::BetaDiskBus *bd=zx.getBetaDiskForTesting();
      speccy=(sp!=0);beta=(bd!=0);
      if(speccy && beta)
        throw std::runtime_error("Spectrum disk manager: conflicting controllers");
      if(sp)selectedDrive=sp->getDriveNumber();
      if(bd)selectedDrive=bd->getSelectedDriveNumber();
      for(unsigned n=0;n<4;++n) {
        if(sp) {
          Ep128Emu::FloppyDrive& drive=sp->getDrive(n);
          mounted[n]=drive.haveDisk();
          paths[n]=(n==0 ? g.speccyDOSMountedSDDPath : g.c09SpeccyImagePath[n]);
          protectedMedia[n]=(mounted[n] ? drive.getIsWriteProtected() :
              (n==0 ? g.speccyDOSWriteProtect : g.c09SpeccyWriteProtect[n]));
          canProtect[n]=!mounted[n] || drive.isNativeSDDWritable();
        }
        else if(bd) {
          mounted[n]=bd->isDiskMounted(n);
          paths[n]=g.c09BetaImagePath[n];
          protectedMedia[n]=(mounted[n] ? bd->isWriteProtected(n) :
              (n==0 ? g.betaDiskWriteProtect : g.c09BetaWriteProtect[n]));
          canProtect[n]=!mounted[n] || bd->isWritableCopyMounted(n);
        }
      }
    }
    catch (const std::exception& e) {
      g.unlockVMThread();g.errorMessage(e.what());return;
    }
    g.unlockVMThread();
    // Snapshot the GUI-owned firmware lifecycle after reading its VM state.
    const unsigned controllerGeneration=c09ZXDiskControllerGeneration;

    // Use the ORIGINAL FLTK Floppy tab and all four existing drive rows.
    // The temporary callbacks are restored before any C09 VM media operation.
    Ep128EmuGUI_DiskConfigWindow& d=*g.diskConfigWindow;
    Fl_Window& window=*d.window;
    C09SharedDiskWindow sharedShell(d);
    C09ZXDiskSelection chosen={C09Close,0,&window,&g,
                               &g.diskImageDirectory,speccy,""};
    C09ZXDiskButton buttons[15];int nButtons=0;
    const bool active=speccy||beta;
    // Initial snapshot is not enough: TR-DOS auto-enter may finish while
    // this modeless dialog remains open. Track it and re-enable actions
    // when the short guest keyboard sequence releases its safety guard.
    const bool idleInput=g.betaDOSInputStep<0 && g.archiveAutoRunPhase<0;
    bool lastInputReady=idleInput;
    Fl_Button *inserts[4]={d.floppyAInsertButton,d.floppyBInsertButton,
                           d.floppyCInsertButton,d.floppyDInsertButton};
    Fl_Button *ejects[4]={d.floppyAEjectButton,d.floppyBEjectButton,
                          d.floppyCEjectButton,d.floppyDEjectButton};
    Fl_Button *protects[4]={d.floppyAProtectButton,d.floppyBProtectButton,
                            d.floppyCProtectButton,d.floppyDProtectButton};
    Fl_Box *mediaInfo[4]={d.floppyAMediaInfo,d.floppyBMediaInfo,
                          d.floppyCMediaInfo,d.floppyDMediaInfo};
    Fl_Input *filenames[4]={d.floppyAImageFileValuator,
                            d.floppyBImageFileValuator,
                            d.floppyCImageFileValuator,
                            d.floppyDImageFileValuator};
    Fl_Value_Input *tracks[4]={d.floppyATracksValuator,
                               d.floppyBTracksValuator,
                               d.floppyCTracksValuator,
                               d.floppyDTracksValuator};
    Fl_Value_Input *sides[4]={d.floppyASidesValuator,
                              d.floppyBSidesValuator,
                              d.floppyCSidesValuator,
                              d.floppyDSidesValuator};
    Fl_Value_Input *sectors[4]={d.floppyASectorsPerTrackValuator,
                                d.floppyBSectorsPerTrackValuator,
                                d.floppyCSectorsPerTrackValuator,
                                d.floppyDSectorsPerTrackValuator};
    Fl_Choice *bytes[4]={d.floppyABytesPerSectorValuator,
                         d.floppyBBytesPerSectorValuator,
                         d.floppyCBytesPerSectorValuator,
                         d.floppyDBytesPerSectorValuator};
    d.diskConfigTabs->value(d.diskConfigTabs->child(0)); // Floppy
    for(int n=0;n<4;++n) {
      sharedShell.remember(inserts[n]);
      sharedShell.remember(ejects[n]);
      sharedShell.remember(protects[n]);
      sharedShell.remember(filenames[n]);
      sharedShell.remember(mediaInfo[n]);
      sharedShell.remember(tracks[n]);
      sharedShell.remember(sides[n]);
      sharedShell.remember(sectors[n]);
      sharedShell.remember(bytes[n]);
      filenames[n]->value(mounted[n] ? paths[n].c_str() : "");
      filenames[n]->readonly(1);  // Existing EP media configuration is untouched.
      // Original legacy editable inputs have a bright yellow caret (color 3).
      // Hide that caret in ZX's read-only display fields, not in EP/CPC/TVC.
      filenames[n]->cursor_color(filenames[n]->color());
      if(n>=2)filenames[n]->copy_label(
          speccy ? (n==2?"2: ":"3: ") : (n==2?"C: ":"D: "));
      if(n<2) {
        Fl_Button *physical=(n==0?d.floppyADeviceButton:d.floppyBDeviceButton);
        sharedShell.remember(physical);
        physical->copy_label(speccy?(n==0?"0:":"1:"):(n==0?"A:":"B:"));
        physical->deactivate(); // EP host floppy-device picker is not ZX media.
      }
      // The ZX ROM/controller determines the disk geometry from the media.
      // Showing editable fields without applying them would silently lie.
      int t=-1,h=-1,r=-1,sectorIndex=2;
      char mediaLabel[165]={0};
      if(mounted[n] && !paths[n].empty()) {
        const Ep128Emu::C09DiskMedia m=Ep128Emu::inspectC09DiskMedia(paths[n]);
        if(m.identified()) {
          t=m.tracks;h=m.sides;r=m.sectors;
          sectorIndex=(m.bytesPerSector==128?0:
                       m.bytesPerSector==256?1:
                       m.bytesPerSector==1024?3:2);
          std::snprintf(mediaLabel,sizeof(mediaLabel),"%s",
              speccy?"SpeccyDOS SDD":"TR-DOS TRD");
        }
      }
      mediaInfo[n]->copy_label(mediaLabel);
      tracks[n]->value(double(t));
      sides[n]->value(double(h));
      sectors[n]->value(double(r));
      bytes[n]->value(sectorIndex);
      tracks[n]->deactivate();sides[n]->deactivate();
      sectors[n]->deactivate();bytes[n]->deactivate();
      Fl_Button *actions[3]={inserts[n],ejects[n],protects[n]};
      for(int op=0;op<3;++op) {
        const int action=op==0?C09Insert:op==1?C09Eject:C09Protect;
        buttons[nButtons].sel=&chosen;
        buttons[nButtons].action=action;
        buttons[nButtons].slot=n;
        actions[op]->callback(c09ZXDiskButtonClick,&buttons[nButtons++]);
        if(op==2)actions[op]->copy_label(
            protectedMedia[n]?"Unprotect":"Protect");
        if(!active || (op==0 && mounted[n]) || (op==1 && !mounted[n]) ||
           (op==2 && !canProtect[n]) || (beta && !idleInput))
          actions[op]->deactivate();
        else actions[op]->activate();
      }
    }
    // Legacy Apply/Clear would modify EP/CPC/TVC configuration: hide them
    // while the guest-owned ZX controller is active. Retain genuine tab rows.
    sharedShell.remember(d.floppyClearAllButton);
    sharedShell.remember(d.floppyApplyButton);
    sharedShell.remember(d.floppyOKButton);
    d.floppyClearAllButton->hide();
    d.floppyApplyButton->hide();
    d.floppyOKButton->copy_label("Close");
    buttons[nButtons].sel=&chosen;buttons[nButtons].action=C09Close;
    buttons[nButtons].slot=0;
    d.floppyOKButton->callback(c09ZXDiskButtonClick,&buttons[nButtons++]);
    char guestInfo[160];
    const char *systemLabel=speccy?"SpeccyDOS / SDD":beta?"TR-DOS / TRD":
        "No Spectrum disk controller";
    std::snprintf(guestInfo,sizeof(guestInfo),"%s | Selected drive: %c",
                  systemLabel,
                  speccy?char('0'+selectedDrive):char('A'+selectedDrive));
    // Wider left-aligned label fits both the selected-drive wording and
    // existing Beta Disk buttons beginning at x=355.
    Fl_Box *guest=new Fl_Box(10,413,337,28);
    guest->copy_label(guestInfo);
    guest->labelsize(12);
    guest->align(FL_ALIGN_LEFT|FL_ALIGN_INSIDE);
    if(beta) {
      Fl_Button *enter=new Fl_Button(355,415,110,25,"Enter TR-DOS");
      buttons[nButtons].sel=&chosen;buttons[nButtons].action=C09EnterDOS;
      buttons[nButtons].slot=0;
      enter->callback(c09ZXDiskButtonClick,&buttons[nButtons++]);
      if(!idleInput)enter->deactivate();
      Fl_Button *autob=new Fl_Button(470,415,136,25,
          g.betaDOSAutoEnterOnReset?"Auto-enter: ON":"Auto-enter: OFF");
      buttons[nButtons].sel=&chosen;buttons[nButtons].action=C09AutoDOS;
      buttons[nButtons].slot=0;
      autob->callback(c09ZXDiskButtonClick,&buttons[nButtons++]);
    }
    window.end();
    if(!window.shown())g.positionAuxiliaryWindow(&window);
    window.show();
    // ROM disable invalidates the media and triggers C09Refresh. Showing
    // the refreshed modeless disk form can re-activate its native NSWindow,
    // even if the disable callback already focused the emulator. Re-assert
    // key-window + guest keyboard focus AFTER this show(), only on disable.
    if(focusAfterControllerDisable) {
      focusAfterControllerDisable=false;
      if(!active)
        c09RestoreEmulatorFocusAfterROMDisable(g.mainWindow,g.emulatorWindow);
    }
    // FLTK dispatches other windows' menus even while this callback waits.
    // Close safely if the main emulator window exits or machine type changes.
    while(window.shown() && g.mainWindow->shown() &&
          !g.exitFlag && g.restartMachineType<0 &&
          typeid(g.vm)==typeid(ZX128::ZX128VM)) {
      // C10: C09's nested wait blocks the outer updateDisplay() call, so
      // without this bounded tick even genuine ROM motor LED events never
      // reach the visible status panel. Pump FLTK at ~60 Hz, copy the VM
      // thread's already-published status without a VM mutation/lock, and
      // detect an exiting worker so File/Quit can unwind the dialog.
      (void)Fl::wait(0.016);
      Ep128Emu::VMThread::VMThreadStatus status(g.vmThread);
      c10RenderFloppyDriveLEDs(
          g.driveALEDDisplay,g.driveBLEDDisplay,
          g.driveCLEDDisplay,g.driveDLEDDisplay,
          g.oldFloppyDriveLEDState,status.floppyDriveLEDState);
      // C12: same lock-free published VM status as the A-D LEDs.
      // Only the guest FDC control register changes this indication.
      if(active && status.selectedFloppyDrive<4U &&
         status.selectedFloppyDrive!=selectedDrive) {
        selectedDrive=status.selectedFloppyDrive;
        std::snprintf(guestInfo,sizeof(guestInfo),
                      "%s | Selected drive: %c",systemLabel,
                      speccy?char('0'+selectedDrive):char('A'+selectedDrive));
        guest->copy_label(guestInfo);
        guest->redraw();
      }
      // The old one-time 'idleInput' gate left all Beta Disk Insert
      // buttons disabled forever when the TR-DOS keyboard injection
      // finished after opening the panel. Refresh enabled states on the
      // transition, but keep pending-input guards in the mount handler.
      if(beta) {
        const bool ready=g.betaDOSInputStep<0 && g.archiveAutoRunPhase<0;
        if(ready!=lastInputReady) {
          lastInputReady=ready;
          for(unsigned n=0;n<4U;++n) {
            if(ready && !mounted[n])inserts[n]->activate();
            else inserts[n]->deactivate();
            if(ready && mounted[n])ejects[n]->activate();
            else ejects[n]->deactivate();
            if(ready && canProtect[n])protects[n]->activate();
            else protects[n]->deactivate();
          }
        }
      }
      if(status.threadStatus!=0) {
        g.exitFlag=true;
        chosen.action=C09Close;
        window.hide();
        break;
      }
      if(!window.shown())break;
      // This check runs immediately after a machine-select callback as well
      // as after every ROM enable/disable event. A queued restart has higher
      // priority than redrawing the now-disabled Spectrum panel.
      if(g.restartMachineType>=0) {
        chosen.action=C09Close;
        window.hide();
        break;
      }
      // Opening the disk panel before selecting a ROM is valid. A successful
      // firmware change in another menu must immediately rebuild the actions
      // (Insert/Eject/Protect) without requiring a manual Close + Alt+D.
      if(c09ZXDiskControllerGeneration!=controllerGeneration) {
        if(active)focusAfterControllerDisable=true;
        chosen.action=C09Refresh;
        window.hide();
      }
    }
    if(window.shown())window.hide();
    if(g.restartMachineType>=0)return; // Exit nested FLTK loop for real machine restart.
    if(chosen.action==C09Refresh)continue; // Original FLTK widgets restored by RAII.
    if(chosen.action==C09Close)return;
    if(typeid(g.vm)!=typeid(ZX128::ZX128VM))return;
    try {
      if(chosen.action==C09EnterDOS) {
        if(beta)menuCallback_Machine_BetaDiskEnterDOS((Fl_Widget*)0,&g);
        continue;
      }
      if(chosen.action==C09AutoDOS) {
        if(!beta)continue;
        g.betaDOSAutoEnterOnReset=!g.betaDOSAutoEnterOnReset;
        if(!g.betaDOSAutoEnterOnReset)g.cancelBetaDOSAutoEnter();
        else g.armBetaDOSAutoEnterIfReady();
        g.updateMenu();continue;
      }
      const unsigned slot=unsigned(chosen.slot);
      if(slot>=4 || !active)continue;
      if(beta && (g.betaDOSInputStep>=0 || g.archiveAutoRunPhase>=0))
        throw std::runtime_error("Beta Disk: wait for pending keyboard action");
      const std::string insertPath=chosen.insertPath;
      if(chosen.action==C09Insert) {
        // File selection finished while the shared panel was still shown;
        // the original guest media validation and VM lock stay here.
        if(insertPath.empty())continue;
        const Ep128Emu::C09DiskMedia media=
             Ep128Emu::inspectC09DiskMedia(insertPath);
        if(speccy && media.type!=Ep128Emu::C09DiskMedia::SpeccySDD)
          throw std::runtime_error("SpeccyDOS: use validated 640 KiB .sdd image");
        if(beta && media.type!=Ep128Emu::C09DiskMedia::BetaTRD)
          throw std::runtime_error("Beta Disk: use validated .trd image");
        for(unsigned other=0;other<4;++other)if(other!=slot && mounted[other]
                && c09ZXSameHostImage(insertPath,paths[other]))
          throw std::runtime_error("Disk already mounted in another drive");
      }
      if(!g.lockVMThread())
        throw std::runtime_error("Spectrum disk manager: VM is busy");
      try {
        ZX128::ZX128VM& zx=static_cast<ZX128::ZX128VM&>(g.vm);
        ZX128::SpeccyDOSBus *sp=zx.getSpeccyDOSForTesting();
        ZX128::BetaDiskBus *bd=zx.getBetaDiskForTesting();
        if(speccy!=bool(sp) || beta!=bool(bd))
          throw std::runtime_error("Disk controller changed; retry operation");
        if(chosen.action==C09Insert) {
          // Repeat duplicate-image test after file picker under VM lock.
          for(unsigned other=0;other<4;++other)if(other!=slot) {
            const bool occupied=sp?sp->getDrive(other).haveDisk():
                                   bd->isDiskMounted(other);
            const std::string& old=sp?
                (other==0?g.speccyDOSMountedSDDPath:g.c09SpeccyImagePath[other]):
                g.c09BetaImagePath[other];
            if(occupied && (old.empty() || c09ZXSameHostImage(insertPath,old)))
              throw std::runtime_error("Image already in use or unknown on other drive");
          }
          if(sp) {
            Ep128Emu::FloppyDrive& drive=sp->getDrive(slot);
            if(drive.haveDisk())throw std::runtime_error("Eject old SDD first");
            // Retain the qualified native SDD RW path and its RO fallback.
            try {drive.setDiskImageFile(insertPath,80,2,16,256,false,true);}
            catch (...) {drive.setDiskImageFile(insertPath,80,2,16,256,true);}
            const bool wp=slot==0?g.speccyDOSWriteProtect:
                                       g.c09SpeccyWriteProtect[slot];
            drive.setUserWriteProtected(wp);
            g.c09SpeccyImagePath[slot]=insertPath;
            if(slot==0)g.speccyDOSMountedSDDPath=insertPath;
          }
          else if(bd) {
            if(bd->isDiskMounted(slot))
              throw std::runtime_error("Eject old TRD first");
            const Ep128EmuBetaTrial::StagedTRD media=
                  Ep128EmuBetaTrial::inspectTRDWritableExisting(insertPath);
            std::vector<uint8_t> rom(16384U);
            for(size_t i=0;i<rom.size();++i)
              rom[i]=bd->readROM(uint16_t(i));
            if(Ep128EmuBetaTrial::writableROMApproved(rom)) {
              try {bd->mountWritableTRD(slot,media.path,media.tracks,media.sides);}
              catch (...) {bd->mountProtectedExisting(slot,media.path,
                                                        media.tracks,media.sides);}
            }
            else bd->mountProtectedExisting(slot,media.path,
                                              media.tracks,media.sides);
            const bool wp=slot==0?g.betaDiskWriteProtect:
                                       g.c09BetaWriteProtect[slot];
            bd->setWriteProtected(slot,wp || !bd->isWritableCopyMounted(slot));
            g.c09BetaImagePath[slot]=insertPath;
          }
          g.vmThread.clearRewindHistory();
        }
        else if(chosen.action==C09Eject) {
          if(sp) {
            Ep128Emu::FloppyDrive& drive=sp->getDrive(slot);
            if(!drive.syncNativeSDD())
              throw std::runtime_error("SpeccyDOS: SDD sync failed; eject cancelled");
            drive.setDiskImageFile("",80,2,16,256);
            g.c09SpeccyImagePath[slot].clear();
            if(slot==0)g.speccyDOSMountedSDDPath.clear();
          }
          else if(bd) {
            bd->ejectReadOnly(slot);
            g.c09BetaImagePath[slot].clear();
          }
          g.vmThread.clearRewindHistory();
        }
        else if(chosen.action==C09Protect) {
          if(sp) {
            Ep128Emu::FloppyDrive& drive=sp->getDrive(slot);
            if(drive.haveDisk() && !drive.isNativeSDDWritable())
              throw std::runtime_error("SpeccyDOS: read-only image; eject to change mode");
            const bool wp=!(slot==0?g.speccyDOSWriteProtect:
                                      g.c09SpeccyWriteProtect[slot]);
            g.c09SpeccyWriteProtect[slot]=wp;
            if(slot==0)g.speccyDOSWriteProtect=wp;
            if(drive.haveDisk())drive.setUserWriteProtected(wp);
          }
          else if(bd) {
            if(bd->isDiskMounted(slot) && !bd->isWritableCopyMounted(slot))
              throw std::runtime_error("Beta Disk: image/ROM is read-only");
            const bool wp=!(slot==0?g.betaDiskWriteProtect:
                                      g.c09BetaWriteProtect[slot]);
            g.c09BetaWriteProtect[slot]=wp;
            if(slot==0)g.betaDiskWriteProtect=wp;
            if(bd->isDiskMounted(slot))bd->setWriteProtected(slot,wp);
          }
        }
      }
      catch (...) {g.unlockVMThread();throw;}
      g.unlockVMThread();
      g.updateMenu();
      if(beta && chosen.action==C09Insert)
        g.armBetaDOSAutoEnterIfReady();
    }
    catch (const std::exception& e) {g.errorMessage(e.what());}
  }
