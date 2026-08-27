//
// Copyright 2020 Electronic Arts Inc.
//
// TiberianDawn.DLL and RedAlert.dll and corresponding source code is free
// software: you can redistribute it and/or modify it under the terms of
// the GNU General Public License as published by the Free Software Foundation,
// either version 3 of the License, or (at your option) any later version.

// TiberianDawn.DLL and RedAlert.dll and corresponding source code is distributed
// in the hope that it will be useful, but with permitted additional restrictions
// under Section 7 of the GPL. See the GNU General Public License in LICENSE.TXT
// distributed with this program. You should have received a copy of the
// GNU General Public License along with permitted additional restrictions
// with this program. If not, see https://github.com/electronicarts/CnC_Remastered_Collection

/* $Header:   F:\projects\c&c\vcs\code\drive.cpv   2.17   16 Oct 1995 16:51:16   JOE_BOSTIC  $ */
/***********************************************************************************************
 ***             C O N F I D E N T I A L  ---  W E S T W O O D   S T U D I O S               ***
 ***********************************************************************************************
 *                                                                                             *
 *                 Project Name : Command & Conquer                                            *
 *                                                                                             *
 *                    File Name : DRIVE.CPP                                                    *
 *                                                                                             *
 *                   Programmer : Joe L. Bostic                                                *
 *                                                                                             *
 *                   Start Date : April 22, 1994                                               *
 *                                                                                             *
 *                  Last Update : July 30, 1995 [JLB]                                          *
 *                                                                                             *
 *---------------------------------------------------------------------------------------------*
 * Functions:                                                                                  *
 *   DriveClass::AI -- Processes unit movement and rotation.                                   *
 *   DriveClass::Approach_Target -- Handles approaching the target in order to attack it.      *
 *   DriveClass::Assign_Destination -- Set the unit's NavCom.                                  *
 *   DriveClass::Class_Of -- Fetches a reference to the class type for this object.            *
 *   DriveClass::Debug_Dump -- Displays status information to monochrome screen.               *
 *   DriveClass::Do_Turn -- Tries to turn the vehicle to the specified direction.              *
 *   DriveClass::DriveClass -- Constructor for drive class object.                             *
 *   DriveClass::Exit_Map -- Give the unit a movement order to exit the map.                   *
 *   DriveClass::Fixup_Path -- Adds smooth start path to normal movement path.                 *
 *   DriveClass::Force_Track -- Forces the unit to use the indicated track.                    *
 *   DriveClass::Lay_Track -- Handles track laying logic for the unit.                         *
 *   DriveClass::Offload_Tiberium_Bail -- Offloads one Tiberium quantum from the object.       *
 *   DriveClass::Ok_To_Move -- Checks to see if this object can begin moving.                  *
 *   DriveClass::Overrun_Square -- Handles vehicle overrun of a cell.                          *
 *   DriveClass::Per_Cell_Process -- Handles when unit finishes movement into a cell.          *
 *   DriveClass::Smooth_Turn -- Handles the low level coord calc for smooth turn logic.        *
 *   DriveClass::Start_Of_Move -- Tries to get a unit to advance toward cell.                  *
 *   DriveClass::Tiberium_Load -- Determine the Tiberium load as a percentage.                 *
 *   DriveClass::While_Moving -- Processes unit movement.                                      *
 *   DriveClass::Mark_Track -- Marks the midpoint of the track as occupied.                    *
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

#include "function.h"

DriveClass::DriveClass(void)
    : Class(0)
    , SimLeptonX(0)
    , SimLeptonY(0){}; // Added SimLeptonX and Y. ST - 4/30/2019 8:06AM

/***********************************************************************************************
 * DriveClass::Do_Turn -- Tries to turn the vehicle to the specified direction.                *
 *                                                                                             *
 *    This routine will set the vehicle to rotate to the direction specified. For tracked      *
 *    vehicles, it is just a simple rotation. For wheeled vehicles, it performs a series       *
 *    of short drives (three point turn) to face the desired direction.                        *
 *                                                                                             *
 * INPUT:   dir   -- The direction that this vehicle should face.                              *
 *                                                                                             *
 * OUTPUT:  none                                                                               *
 *                                                                                             *
 * WARNINGS:   none                                                                            *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   05/29/1995 JLB : Created.                                                                 *
 *=============================================================================================*/
void DriveClass::Do_Turn(DirType dir)
{
    if (dir != PrimaryFacing) {

        /*
        **	Special rotation track is needed for units that
        **	cannot rotate in place.
        */
        if (Special.IsThreePoint && TrackNumber == -1 && Class->Speed == SPEED_WHEEL) {
            int facediff;    // Signed difference between current and desired facing.
            FacingType face; // Current facing (ordinal value).

            facediff = PrimaryFacing.Difference(dir) >> 5;
            facediff = Bound(facediff, -2, 2);
            if (facediff) {
                face = Dir_Facing(PrimaryFacing);

                IsOnShortTrack = true;
                Force_Track(face * FACING_COUNT + (face + facediff), Coord);

                Path[0] = FACING_NONE;
                Set_Speed(0xFF); // Full speed.
            }
        } else {
            PrimaryFacing.Set_Desired(dir);
            // if (Special.IsJurassic && AreThingiesEnabled && What_Am_I() == RTTI_UNIT && ((UnitClass
            // *)this)->Class->IsPieceOfEight) PrimaryFacing.Set_Current(dir);
            if (What_Am_I() == RTTI_UNIT && ((UnitClass*)this)->Class->IsPieceOfEight)
                PrimaryFacing.Set_Current(dir);
        }
    }
}

/***********************************************************************************************
 * DriveClass::Force_Track -- Forces the unit to use the indicated track.                      *
 *                                                                                             *
 *    This override (nuclear bomb) style routine is to be used when a unit needs to start      *
 *    on a movement track but is outside the normal movement system. This occurs when a        *
 *    harvester starts driving off of a refinery.                                              *
 *                                                                                             *
 * INPUT:   track -- The track number to start on.                                             *
 *                                                                                             *
 *          coord -- The coordinate that the unit will end up at when the movement track       *
 *                   is completed.                                                             *
 *                                                                                             *
 * OUTPUT:  none                                                                               *
 *                                                                                             *
 * WARNINGS:   none                                                                            *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   03/17/1995 JLB : Created.                                                                 *
 *=============================================================================================*/
void DriveClass::Force_Track(int track, COORDINATE coord)
{
    TrackNumber = track;
    TrackIndex = 0;
    Start_Driver(coord);
}

/***********************************************************************************************
 * DriveClass::Tiberium_Load -- Determine the Tiberium load as a percentage.                   *
 *                                                                                             *
 *    Use this routine to determine what the Tiberium load is (as a fixed point percentage).   *
 *                                                                                             *
 * INPUT:   none                                                                               *
 *                                                                                             *
 * OUTPUT:  Returns with the current "fullness" rating for the object. This will be 0x0000 for *
 *          empty and 0x0100 for full.                                                         *
 *                                                                                             *
 * WARNINGS:   none                                                                            *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   03/17/1995 JLB : Created.                                                                 *
 *=============================================================================================*/
int DriveClass::Tiberium_Load(void) const
{
    if (*this == UNIT_HARVESTER) {
        return (Cardinal_To_Fixed(UnitTypeClass::STEP_COUNT, Tiberium));
    }
    return (0x0000);
}

/***********************************************************************************************
 * DriveClass::Approach_Target -- Handles approaching the target in order to attack it.        *
 *                                                                                             *
 *    This routine will check to see if the target is infantry and it can be overrun. It will  *
 *    try to overrun the infantry rather than attack it. This only applies to computer         *
 *    controlled vehicles. If it isn't the infantry overrun case, then it falls into the       *
 *    base class for normal (complex) approach algorithm.                                      *
 *                                                                                             *
 * INPUT:   none                                                                               *
 *                                                                                             *
 * OUTPUT:  none                                                                               *
 *                                                                                             *
 * WARNINGS:   none                                                                            *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   03/17/1995 JLB : Created.                                                                 *
 *   07/12/1995 JLB : Flamethrower tanks don't overrun -- their weapon is better.              *
 *=============================================================================================*/
void DriveClass::Approach_Target(void)
{
    /*
    **	Only if there is a legal target should the approach check occur.
    */
    if (!House->IsHuman && Target_Legal(TarCom) && !Target_Legal(NavCom)) {

        /*
        **	Special case:
        **	If this is for a unit that can crush infantry, and the target is
        **	infantry, AND the infantry is pretty darn close, then just try
        **	to drive over the infantry instead of firing on it.
        */
        TechnoClass* target = As_Techno(TarCom);
        if (Class->Primary != WEAPON_FLAME_TONGUE && Class->IsCrusher && Distance(TarCom) < 0x0180 && target
            && ((TechnoTypeClass const&)(target->Class_Of())).IsCrushable) {
            Assign_Destination(TarCom);
            return;
        }
    }

    /*
    **	In the other cases, uses the more complex "get to just within weapon range"
    **	algorithm.
    */
    FootClass::Approach_Target();
}

/***********************************************************************************************
 * DriveClass::Overrun_Square -- Handles vehicle overrun of a cell.                            *
 *                                                                                             *
 *    This routine is called when a vehicle enters a square or when it is about to enter a     *
 *    square (controlled by parameter). When a vehicle that can crush infantry enters a        *
 *    cell that contains infantry, then the infantry will be destroyed (regardless of          *
 *    affiliation). When a vehicle threatens to overrun a square, all occupying infantry       *
 *    will attempt to get out of the way.                                                      *
 *                                                                                             *
 * INPUT:   cell     -- The cell that is, or soon will be, entered by a vehicle.               *
 *                                                                                             *
 *          threaten -- Don't kill, but just threaten to enter the cell.                       *
 *                                                                                             *
 * OUTPUT:  none                                                                               *
 *                                                                                             *
 * WARNINGS:   none                                                                            *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   01/19/1995 JLB : Created.                                                                 *
 *=============================================================================================*/
void DriveClass::Overrun_Square(CELL cell, bool threaten)
{
    CellClass* cellptr = &Map[cell];

    if (Class->IsCrusher) {
        if (threaten) {

            /*
            **	If the cell contains infantry, then they will panic when a vehicle tries
            **	drive over them. Have the infantry run away instead.
            */
            if (cellptr->Flag.Composite & 0x1F) {

                /*
                **	Scattering is controlled by the game difficulty level.
                */
                if (((GameToPlay == GAME_NORMAL && PlayerPtr->Difficulty == DIFF_HARD) || Special.IsScatter
                     || Scen.Scenario > 8)
                    && !(GameToPlay == GAME_NORMAL && PlayerPtr->Difficulty == DIFF_EASY)) {
                    cellptr->Incoming(0, true);
                }
            }
        } else {
            ObjectClass* object = cellptr->Cell_Occupier();
            int crushed = false;
            while (object) {
                /*
                **	CNC3D_Is_Invincible is tested here as well as in Take_Damage because this
                **	is the ONE way an object dies without damage being applied to it: crushing
                **	does not call Take_Damage at all, it goes straight to Record_The_Kill,
                **	Limbo and delete a few lines below. Without this term an invincible house
                **	would still lose every infantryman a tracked vehicle drove over.
                */
                if (object->Class_Of().IsCrushable && !House->Is_Ally(object)
                    && !object->CNC3D_Is_Invincible() && Distance(object->Center_Coord()) < 0x80) {
                    ObjectClass* next = object->Next;
                    crushed = true;

                    /*
                    ** Record credit for the kill(s)
                    */
                    Sound_Effect(VOC_SQUISH2, Coord);
                    object->Record_The_Kill(this);
                    object->Mark(MARK_UP);
                    object->Limbo();
                    delete object;
                    new OverlayClass(OVERLAY_SQUISH, Coord_Cell(Coord));

                    object = next;
                } else {
                    object = object->Next;
                }
            }
            if (crushed)
                Do_Uncloak();
        }
    }
}

/***********************************************************************************************
 * DriveClass::DriveClass -- Constructor for drive class object.                               *
 *                                                                                             *
 *    This will initialize the drive class to its default state. It is called as a result      *
 *    of creating a unit.                                                                      *
 *                                                                                             *
 * INPUT:   classid  -- The unit's ID class. It is passed on to the foot class constructor.    *
 *                                                                                             *
 * OUTPUT:  none                                                                               *
 *                                                                                             *
 * WARNINGS:   none                                                                            *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   07/13/1994 JLB : Created.                                                                 *
 *=============================================================================================*/
DriveClass::DriveClass(UnitType classid, HousesType house)
    : Class(&UnitTypeClass::As_Reference(classid))
    , FootClass(house)
{
    /*
    **	For two shooters, clear out the second shot flag -- it will be set the first time
    **	the object fires. For non two shooters, set the flag since it will never be cleared
    **	and the second shot flag tells the system that normal rearm times apply -- this is
    **	what is desired for non two shooters.
    */
    if (Class->IsTwoShooter) {
        IsSecondShot = false;
    } else {
        IsSecondShot = true;
    }
    IsHarvesting = false;
    IsTurretLockedDown = false;
    IsOnShortTrack = false;
    IsReturning = false;
    TrackNumber = -1;
    TrackIndex = 0;
    SpeedAccum = 0;
    Tiberium = 0;
    Strength = Class->MaxStrength;
}

#ifdef CHEAT_KEYS
/***********************************************************************************************
 * DriveClass::Debug_Dump -- Displays status information to monochrome screen.                 *
 *                                                                                             *
 *    This debug utility function will display the status of the drive class to the mono       *
 *    screen. It is through this information that bugs can be tracked down.                    *
 *                                                                                             *
 * INPUT:   none                                                                               *
 *                                                                                             *
 * OUTPUT:  none                                                                               *
 *                                                                                             *
 * WARNINGS:   none                                                                            *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   05/31/1994 JLB : Created.                                                                 *
 *=============================================================================================*/
void DriveClass::Debug_Dump(MonoClass* mono) const
{
    mono->Set_Cursor(33, 7);
    mono->Printf("%2d:%2d", TrackNumber, TrackIndex);
    mono->Text_Print("X", 16 + (IsTurretLockedDown ? 2 : 0), 10);
    //	mono->Text_Print("X", 16 + (IsOnShortTrack?2:0), 11);
    mono->Set_Cursor(41, 7);
    mono->Printf("%d", Fixed_To_Cardinal(100, Tiberium_Load()));
    FootClass::Debug_Dump(mono);
}
#endif

/***********************************************************************************************
 * DriveClass::Exit_Map -- Give the unit a movement order to exit the map.                     *
 *                                                                                             *
 *    This routine is used to assign an appropriate movement destination for the unit so that  *
 *    it will leave the map. The scripts are usually the one to call this routine when it      *
 *    is determined that the unit has fulfilled its mission and must "depart".                 *
 *                                                                                             *
 * INPUT:   none                                                                               *
 *                                                                                             *
 * OUTPUT:  none                                                                               *
 *                                                                                             *
 * WARNINGS:   none                                                                            *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   05/31/1994 JLB : Created.                                                                 *
 *=============================================================================================*/
void DriveClass::Exit_Map(void)
{
    CELL cell; // Map exit cell number.

    if (*this == UNIT_HOVER && !Target_Legal(NavCom)) {

        /*
        **	Scan a swath of cells from current position to the edge of the map and if
        **	there is any blocking object, just wait so to try again later.
        */
        Mark(MARK_UP);
        for (int x = Cell_X(Coord_Cell(Center_Coord())) - 1; x <= Cell_X(Coord_Cell(Center_Coord())) + 1; x++) {
            for (int y = Cell_Y(Coord_Cell(Center_Coord())) + 1; y < Map.MapCellY + Map.MapCellHeight; y++) {
                cell = XY_Cell(x, y);
                if (Map[cell].Cell_Techno()) {
                    Mark(MARK_DOWN);
                    return;
                }
            }
        }
        Mark(MARK_DOWN);

        /*
        **	A clear path to the map edge exists. Assign it as the navigation computer
        **	destination and let the transport move.
        */
        cell = XY_Cell(Cell_X(Coord_Cell(Coord)), Map.MapCellY + Map.MapCellHeight);
        IsReturning = true;
        Assign_Destination(::As_Target(cell));
    }
}

/***********************************************************************************************
 * DriveClass::Smooth_Turn -- Handles the low level coord calc for smooth turn logic.          *
 *                                                                                             *
 *    This routine calculates the new coordinate value needed for the                          *
 *    smooth turn logic. The adjustment and flag values must be                                *
 *    determined prior to entering this routine.                                               *
 *                                                                                             *
 * INPUT:   adj      -- The adjustment coordinate as lifted from the                           *
 *                      correct smooth turn table.                                             *
 *                                                                                             *
 *          dir      -- Pointer to dir for possible modification                               *
 *                      according to the flag bits.                                            *
 *                                                                                             *
 * OUTPUT:  Returns with the coordinate the unit should positioned to.                         *
 *                                                                                             *
 * WARNINGS:   none                                                                            *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   03/14/1994 JLB : Created.                                                                 *
 *   07/13/1994 JLB : Converted to member function.                                            *
 *=============================================================================================*/
COORDINATE DriveClass::Smooth_Turn(COORDINATE adj, DirType* dir)
{
    DirType workdir = *dir;
    int x, y;
    int temp;
    TrackControlType flags = TrackControl[TrackNumber].Flag;

    x = Coord_X(adj);
    y = Coord_Y(adj);

    if (flags & F_T) {
        temp = x;
        x = y;
        y = temp;
        workdir = (DirType)(DIR_W - workdir);
    }

    if (flags & F_X) {
        x = -x;
        workdir = (DirType)-workdir;
    }

    if (flags & F_Y) {
        y = -y;
        workdir = (DirType)(DIR_S - workdir);
    }

    *dir = workdir;

    return (XY_Coord(Coord_X(Head_To_Coord()) + x, Coord_Y(Head_To_Coord()) + y));
}

/***********************************************************************************************
 * DriveClass::Assign_Destination -- Set the unit's NavCom.                                    *
 *                                                                                             *
 *    This routine is used to set the unit's navigation computer to the                        *
 *    specified target. Once the navigation computer is set, the unit                          *
 *    will start planning and moving toward the destination.                                   *
 *                                                                                             *
 * INPUT:   target   -- The destination target for the unit to head to.                        *
 *                                                                                             *
 * OUTPUT:  none                                                                               *
 *                                                                                             *
 * WARNINGS:   none                                                                            *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   09/07/1992 JLB : Created.                                                                 *
 *   04/15/1994 JLB : Converted to member function.                                            *
 *=============================================================================================*/
void DriveClass::Assign_Destination(TARGET target)
{

    /*
    **	Abort early if there is anything wrong with the parameters
    **	or the unit already is assigned the specified destination.
    */
    if (target == NavCom)
        return;

#ifdef NEVER
    UnitClass* tunit; // Destination unit pointer.

    /*
    ** When in move mode, a map position may really indicate
    **	a unit to guard.
    */
    if (Is_Target_Cell(target)) {
        cell = As_Cell(target);

        tunit = Map[cell].Cell_Unit();
        if (tunit) {

            /*
            **	Prevent targeting of itself.
            */
            if (tunit != this) {
                target = tunit->As_Target();
            }
        } else {

            tbuilding = Map[cell].Cell_Building();
            if (tbuilding) {
                target = tbuilding->As_Target();
            }
        }
    }
#endif

    /*
    **	For harvesting type vehicles, it might go into a dock and unload procedure
    **	when the harvester is full and an empty refinery is selected as a target.
    */
    BuildingClass* b = As_Building(target);

    /*
    **	Transport vehicles must tell all passengers that are about to load, that they
    **	cannot proceed. This is accomplished with a radio message to this effect.
    */
    // if (tunit && In_Radio_Contact() && Class->IsTransporter && Contact_With_Whom()->Is_Infantry()) {
    if (In_Radio_Contact() && Class->IsTransporter && Contact_With_Whom()->Is_Infantry()) {
        Transmit_Message(RADIO_OVER_OUT);
    }

    /*
    **	If the player clicked on a friendly repair facility and the repair
    **	facility is currently not involved with some other unit (radio or unloading).
    */
    if (b && *b == STRUCT_REPAIR) {
        if (b->In_Radio_Contact() && (b->Contact_With_Whom() != this)) {
            ArchiveTarget = target;
        } else {

            /*
            **	Establish radio contact protocol. If the facility responds correctly,
            **	then remain in radio contact and proceed toward the desired destination.
            */
            if (Transmit_Message(RADIO_HELLO, b) == RADIO_ROGER) {

                /*
                **	Last check to make sure that the loading square is free from permanent
                **	occupation (such as a building).
                */
                CELL cell = Coord_Cell(b->Center_Coord()) + (MAP_CELL_W - 1);
                if (Ground[Map[cell].Land_Type()].Cost[Class->Speed]) {
                    if (Transmit_Message(RADIO_DOCKING) == RADIO_ROGER) {
                        FootClass::Assign_Destination(target);
                        Path[0] = FACING_NONE;
                        return;
                    }

                    /*
                    **	Failure to establish a docking relationship with the refinery.
                    **	Bail & await further instructions.
                    */
                    Transmit_Message(RADIO_OVER_OUT);
                }
            }
        }
    }

    /*
    **	Set the unit's navigation computer.
    */
    FootClass::Assign_Destination(target);
    Path[0] = FACING_NONE; // Force recalculation of path.
    if (!IsDriving) {
        Start_Of_Move();
    }
}

/***********************************************************************************************
 * DriveClass::While_Moving -- Processes unit movement.                                        *
 *                                                                                             *
 *    This routine is used to process movement for the units as they move.                     *
 *    It is called many times for each cell's worth of movement.   This                        *
 *    routine only applies after the next cell HeadTo has been determined.                     *
 *                                                                                             *
 * INPUT:   none                                                                               *
 *                                                                                             *
 * OUTPUT:  true/false; Should this routine be called again?                                   *
 *                                                                                             *
 * WARNINGS:   none                                                                            *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   02/02/1992 JLB : Created.                                                                 *
 *   04/15/1994 JLB : Converted to member function.                                            *
 *=============================================================================================*/
bool DriveClass::While_Moving(void)
{
    int actual; // Working movement addition value.

    /*
    **	Perform quick legality checks.
    */
    if (!IsDriving || TrackNumber == -1 || (IsRotating && !Class->IsTurretEquipped)) {
        SpeedAccum = 0; // Kludge?  No speed should accumulate if movement is on hold.
        return (false);
    }

    /*
    **	If enough movement has accumulated so that the unit can
    **	visibly move on the map, then process accordingly.
    ** Slow the unit down if he's carrying a flag.
    */
    MPHType maxspeed = MPHType(min((int)(Class->MaxSpeed * House->GroundspeedBias), (int)MPH_LIGHT_SPEED));
    if (((UnitClass*)this)->Flagged != HOUSE_NONE) {
        actual = SpeedAccum + Fixed_To_Cardinal(maxspeed / 2, Speed);
    } else {
        actual = SpeedAccum + Fixed_To_Cardinal(maxspeed, Speed);
    }

    if (actual > PIXEL_LEPTON_W) {
        TurnTrackType const* track; // Track control pointer.
        TrackType const* ptr;       // Pointer to coord offset values.
        int tracknum;               // The track number being processed.
        FacingType nextface;        // Next facing queued in path.
        bool adj;                   // Is a turn coming up?

        track = &TrackControl[TrackNumber];
        if (IsOnShortTrack) {
            tracknum = track->StartTrack;
        } else {
            tracknum = track->Track;
        }
        ptr = RawTracks[tracknum - 1].Track;
        nextface = Path[0];

        /*
        **	Determine if there is a turn coming up. If there is
        **	a turn, then track jumping might occur.
        */
        adj = false;
        if (nextface != FACING_NONE && Dir_Facing(track->Facing) != nextface) {
            adj = true;
        }

        /*
        **	Skip ahead the number of track steps required (limited only
        **	by track length). Set the unit to the new position and
        **	flag the unit accordingly.
        */
        Mark(MARK_UP);
        while (actual > PIXEL_LEPTON_W) {
            COORDINATE offset;
            DirType dir;

            actual -= PIXEL_LEPTON_W;

            offset = ptr[TrackIndex].Offset;
            if (offset || !TrackIndex) {
                dir = ptr[TrackIndex].Facing;
                Coord = Smooth_Turn(offset, &dir);

                PrimaryFacing.Set(dir);

                /*
                **	See if "per cell" processing is necessary.
                */
                if (TrackIndex && RawTracks[tracknum - 1].Cell == TrackIndex) {
                    Per_Cell_Process(false);
                    if (!IsActive) {
                        return (false);
                    }
                }

                /*
                **	The unit could "jump tracks". Check to see if the unit should
                **	do so.
                */
                if (*this != UNIT_GUNBOAT && nextface != FACING_NONE && adj
                    && RawTracks[tracknum - 1].Jump == TrackIndex && TrackIndex) {
                    TurnTrackType const* newtrack; // Proposed jump-to track.
                    int tnum;

                    tnum = Dir_Facing(track->Facing) * FACING_COUNT + nextface;
                    newtrack = &TrackControl[tnum];
                    if (newtrack->Track && RawTracks[newtrack->Track - 1].Entry) {
                        COORDINATE c = Head_To_Coord();
                        int oldspeed = Speed;

                        c = Adjacent_Cell(c, nextface);

                        switch (Can_Enter_Cell(Coord_Cell(c), nextface)) {
                        case MOVE_OK:
                            IsOnShortTrack = false; // Shouldn't be necessary, but...
                            TrackNumber = tnum;
                            track = newtrack;

                            //			Mono_Printf("**Jumping from track %d to track %d. **\n", tracknum,
                            //track->Track);Keyboard::Get();

                            tracknum = track->Track;
                            TrackIndex = RawTracks[tracknum - 1].Entry - 1; // Anticipate increment.
                            ptr = RawTracks[tracknum - 1].Track;
                            adj = false;

                            Stop_Driver();
                            Per_Cell_Process(true);
                            if (Start_Driver(c)) {
                                Set_Speed(oldspeed);
                                memcpy(&Path[0], &Path[1], CONQUER_PATH_MAX - 1);
                                Path[CONQUER_PATH_MAX - 1] = FACING_NONE;
                            } else {
                                Path[0] = FACING_NONE;
                                TrackNumber = -1;
                                actual = 0;
                            }
                            break;

                        case MOVE_CLOAK:
                            Map[Coord_Cell(c)].Shimmer();
                            break;

                        case MOVE_TEMP:
                            if (*this == UNIT_HARVESTER || !House->IsHuman) {
                                bool old = Special.IsScatter;
                                Special.IsScatter = true;
                                Map[Coord_Cell(c)].Incoming(0, true);
                                Special.IsScatter = old;
                            }
                            break;
                        }
                    }
                }
                TrackIndex++;

            } else {
                actual = 0;
                Coord = Head_To_Coord();
                Stop_Driver();
                TrackNumber = -1;
                TrackIndex = 0;

                /*
                **	Perform "per cell" activities.
                */
                Per_Cell_Process(true);

                break;
            }
        }
        if (IsActive) {
            Mark(MARK_DOWN);
        }
    }

    /*
    **  NEW 4/30/2019 7:59AM
    **
    **  When we don't have enough speed accumulated to move another pixel, it would be good to know at a sub-pixel
    *(lepton) level
    **  how far we would move if we could. It didn't matter in the original when it was 320x200 pixels, but on a
    *3840x2160
    **  screen, what was half a pixel could now be several pixels.
    **
    **  ST
    **
    */
    if (actual && actual <= PIXEL_LEPTON_W) {
        TurnTrackType const* track; // Track control pointer.
        TrackType const* ptr;       // Pointer to coord offset values.
        int tracknum;               // The track number being processed.
        FacingType nextface;        // Next facing queued in path.
        bool adj;                   // Is a turn coming up?

        track = &TrackControl[TrackNumber];
        if (IsOnShortTrack) {
            tracknum = track->StartTrack;
        } else {
            tracknum = track->Track;
        }
        ptr = RawTracks[tracknum - 1].Track;
        nextface = Path[0];

        /*
        **	Determine if there is a turn coming up. If there is
        **	a turn, then track jumping might occur.
        */
        adj = false;
        if (nextface != FACING_NONE && Dir_Facing(track->Facing) != nextface) {
            adj = true;
        }

        COORDINATE simulated_pos = Coord;

        COORDINATE offset;
        DirType dir;

        offset = ptr[TrackIndex].Offset;
        if (offset || !TrackIndex) {
            dir = ptr[TrackIndex].Facing;
            simulated_pos = Smooth_Turn(offset, &dir);
        }

        int x_diff = Coord_X(simulated_pos) - Coord_X(Coord);
        int y_diff = Coord_Y(simulated_pos) - Coord_Y(Coord);

        SimLeptonX = (x_diff * actual) / PIXEL_LEPTON_W;
        SimLeptonY = (y_diff * actual) / PIXEL_LEPTON_W;
    } else {
        SimLeptonX = 0;
        SimLeptonY = 0;
    }

    /*
    **	Replace any remainder back into the unit's movement
    **	accumulator to be processed next pass.
    */
    SpeedAccum = actual;
    return (true);
}

/***********************************************************************************************
 * DriveClass::Per_Cell_Process -- Handles when unit finishes movement into a cell.            *
 *                                                                                             *
 *    This routine is called when a unit has mostly or completely                              *
 *    entered a cell. The unit might be in the middle of a movement track                      *
 *    when this routine is called. It's primary purpose is to perform                          *
 *    sighting and other "per cell" activities.                                                *
 *                                                                                             *
 * INPUT:   center   -- Is the unit safely at the center of a cell?  If it is merely "close"   *
 *                      to the center, then this parameter will be false.                      *
 *                                                                                             *
 * OUTPUT:  none                                                                               *
 *                                                                                             *
 * WARNINGS:   none                                                                            *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   11/03/1993 JLB : Created.                                                                 *
 *   03/30/1994 JLB : Revamped for track system.                                               *
 *   04/15/1994 JLB : Converted to member function.                                            *
 *   06/18/1994 JLB : Converted to virtual function.                                           *
 *   06/18/1994 JLB : Distinguishes between center and near-center conditions.                 *
 *=============================================================================================*/
void DriveClass::Per_Cell_Process(bool center)
{
    CELL cell = Coord_Cell(Coord);

    /*
    **	Check to see if it has reached its destination. If so, then clear the NavCom
    **	regardless of the remaining path list.
    */
    if (center && As_Cell(NavCom) == cell) {
        IsTurretLockedDown = false;
        NavCom = TARGET_NONE;
        Path[0] = FACING_NONE;
    }

#ifdef NEVER
    /*
    **	A "lemon" vehicle will have a tendency to break down as
    **	it moves about the terrain.
    */
    if (Is_A_Lemon) {
        if (Random_Pick(1, 4) == 1) {
            Take_Damage(1);
        }
    }
#endif

    Lay_Track();

    FootClass::Per_Cell_Process(center);
}

/***********************************************************************************************
 * DriveClass::Start_Of_Move -- Tries to get a unit to advance toward cell.                    *
 *                                                                                             *
 *    This will try to start a unit advancing toward the cell it is                            *
 *    facing. It will check for and handle legality and reserving of the                       *
 *    necessary cell.                                                                          *
 *                                                                                             *
 * INPUT:   none                                                                               *
 *                                                                                             *
 * OUTPUT:  true/false; Should this routine be called again because                            *
 *                      initial start operation is temporarily delayed?                        *
 *                                                                                             *
 * WARNINGS:   none                                                                            *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   02/02/1992 JLB : Created.                                                                 *
 *   10/18/1993 JLB : This should be called repeatedly until HeadTo is not NULL.               *
 *   03/16/1994 JLB : Revamped for track logic.                                                *
 *   04/15/1994 JLB : Converted to member function.                                            *
 *   06/19/1995 JLB : Fixed so that it won't fire on ground unnecessarily.                     *
 *   07/13/1995 JLB : Handles bumping into cloaked objects.                                    *
 *=============================================================================================*/
bool DriveClass::Start_Of_Move(void)
{
    FacingType facing; // Direction movement will commence.
    DirType dir;       // Desired actual facing toward destination.
    int facediff;      // Difference between current and desired facing.
    int speed;         // Speed of unit.
    CELL destcell;     // Cell of destination.
    LandType ground;   // Ground unit is entering.
    COORDINATE dest;   // Destination coordinate.

    facing = Path[0];

    if (!Target_Legal(NavCom) && facing == FACING_NONE) {
        IsTurretLockedDown = false;
        Stop_Driver();
        if (Mission == MISSION_MOVE) {
            Enter_Idle_Mode();
        }
        return (false); // Why is it calling this routine!?!
    }

#ifdef NEVER
    /*
    **	Movement start logic can't begin until a unit that requires
    **	a locked down turret gets to a locked down state (i.e., the
    **	turret rotation stops.
    */
    if (ClassF & CLASSF_LOCKTURRET) {
        Set_Secondary_Facing(facing << 5);
        if (Is_Rotating) {
            return (true);
        }
    }
#endif

    /*
    **	Reduce the path length if the target is a unit and the
    **	range to the unit is less than the precalculated path steps.
    */
    if (facing != FACING_NONE) {
        int dist;

        if (Is_Target_Unit(NavCom) || Is_Target_Infantry(NavCom)) {
            dist = Lepton_To_Cell(Distance(NavCom));

            //			if (dist > CELL_LEPTON_W ||
            //				!As_Techno(NavCom)->Techno_Type_Class()->IsCrushable ||
            //				!Class->IsCrusher) {

            if (dist < CONQUER_PATH_MAX) {
                Path[dist] = FACING_NONE;
                facing = Path[0]; // Maybe needed.
            }
            //			}
        }
    }

    /*
    **	If the path is invalid at this point, then generate one. If
    **	generating a new path fails, then abort NavCom.
    */
    if (facing == FACING_NONE) {

        /*
        **	If after a path search, there is still no valid path, then set the
        **	NavCom to null and let the script take care of assigning a new
        **	navigation target.
        */
        if (!PathDelay.Expired()) {
            return (false);
        }
        if (!Basic_Path()) {
            if (Distance(NavCom) < 0x0280 && (Mission == MISSION_MOVE || Mission == MISSION_GUARD_AREA)) {
                Assign_Destination(TARGET_NONE);
            } else {

                /*
                **	If a basic path could be found, but the immediate move destination is
                **	blocked by a friendly temporary blockage, then cause that blockage
                **	to scatter. If the destination is also one cell away, then scatter
                **	regardless of direction.
                */
                CELL ourcell = Coord_Cell(Center_Coord());
                CELL navcell = As_Cell(NavCom);
                CELL cell = -1;
                if (::Distance(ourcell, navcell) < 2) {
                    cell = navcell;
                } else {
                    cell = Adjacent_Cell(ourcell, PrimaryFacing.Current());
                }
                if (Map.In_Radar(cell)) {
                    if (Can_Enter_Cell(cell) == MOVE_TEMP) {
                        CellClass* cellptr = &Map[cell];
                        TechnoClass* blockage = cellptr->Cell_Techno();
                        if (blockage && House->Is_Ally(blockage)) {
                            bool old = Special.IsScatter;
                            Special.IsScatter = true;
                            cellptr->Incoming(0, true);
                            Special.IsScatter = old;
                        }
                    }
                }

                if (TryTryAgain) {
                    TryTryAgain--;
                } else {
                    Assign_Destination(TARGET_NONE);
                    if (IsNewNavCom)
                        Sound_Effect(VOC_SCOLD);
                    IsNewNavCom = false;
                }
            }
            Stop_Driver();
            TrackNumber = -1;
            IsTurretLockedDown = false;
            return (false);
        }

        /*
        **	If a basic path could be found, but the immediate move destination is
        **	blocked by a friendly temporary blockage, then cause that blockage
        **	to scatter.
        */
        CELL cell = Adjacent_Cell(Coord_Cell(Center_Coord()), Path[0]);
        if (Map.In_Radar(cell)) {
            if (Can_Enter_Cell(cell) == MOVE_TEMP) {
                CellClass* cellptr = &Map[cell];
                TechnoClass* blockage = cellptr->Cell_Techno();
                if (blockage && House->Is_Ally(blockage)) {
                    bool old = Special.IsScatter;
                    Special.IsScatter = true;
                    cellptr->Incoming(0, true);
                    Special.IsScatter = old;
                }
            }
        }

        TryTryAgain = PATH_RETRY;
        facing = Path[0];
    }

    if (Class->IsLockTurret || !Class->IsTurretEquipped) {
        IsTurretLockedDown = true;
    }

#ifdef NEVER
    /*
    **	If the turret needs to match the body's facing before
    **	movement can occur, then start it's rotation and
    **	don't start a movement track until it is aligned.
    */
    if (!Ok_To_Move(BodyFacing)) {
        return (true);
    }
#endif

    /*
    **	Determine the coordinate of the next cell to move into.
    */
    dest = Adjacent_Cell(Coord, facing);
    dir = Facing_Dir(facing);

    /*
    **	Set the facing correctly if it isn't already correct. This
    **	means starting a rotation track if necessary.
    */
    facediff = PrimaryFacing.Difference(dir);
    if (facediff) {

        /*
        **	Request a change of facing.
        */
        Do_Turn(dir);
        return (true);

    } else {

        /* NOTE:  Beyond this point, actual track assignment can begin.
        **
        **	If the cell to move into is impassable (probably for some unexpected
        **	reason), then abort the path list and set the speed to zero. The
        ** next time this routine is called, a new path will be generated.
        */
        destcell = Coord_Cell(dest);
        Mark(MARK_UP);
        MoveType cando = Can_Enter_Cell(destcell, facing);
        Mark(MARK_DOWN);

        if (cando != MOVE_OK) {

            if (Mission == MISSION_MOVE && House->IsHuman && Distance(NavCom) < 0x0200) {
                Assign_Destination(TARGET_NONE);
            }

            /*
            **	If a temporary friendly object is blocking the path, then cause it to
            **	get out of the way.
            */
            if (cando == MOVE_TEMP) {
                bool old = Special.IsScatter;
                Special.IsScatter = true;
                Map[destcell].Incoming(0, true);
                Special.IsScatter = old;
            }

            /*
            **	If a cloaked object is blocking, then shimmer the cell.
            */
            if (cando == MOVE_CLOAK) {
                Map[destcell].Shimmer();
            }

            Stop_Driver();
            if (cando != MOVE_MOVING_BLOCK) {
                Path[0] = FACING_NONE; // Path is blocked!
            }

            /*
            ** If blocked by a moving block then just exit start of move and
            ** try again next tick.
            */
            if (cando == MOVE_DESTROYABLE) {
                if (Map[destcell].Cell_Object()) {
                    if (!House->Is_Ally(Map[destcell].Cell_Object())) {
                        Override_Mission(MISSION_ATTACK, Map[destcell].Cell_Object()->As_Target(), TARGET_NONE);
                    }
                } else {
                    if (Map[destcell].Overlay != OVERLAY_NONE
                        && OverlayTypeClass::As_Reference(Map[destcell].Overlay).IsWall) {
                        Override_Mission(MISSION_ATTACK, ::As_Target(destcell), TARGET_NONE);
                    }
                }
            } else {
                if (IsNewNavCom)
                    Sound_Effect(VOC_SCOLD);
            }
            IsNewNavCom = false;
            TrackNumber = -1;
            return (true);
        }

        /*
        **	Determine the speed that the unit can travel to the desired square.
        */
        ground = Map[destcell].Land_Type();
        speed = Ground[ground].Cost[Class->Speed];
        if (!speed)
            speed = 128;

#ifdef NEVER
        /*
        **	Set the jiggle flag if the terrain would cause the unit
        **	to jiggle when travelled over.
        */
        BaseF &= ~BASEF_JIGGLE;
        if (Ground[ground].Jiggle) {
            BaseF |= BASEF_JIGGLE;
        }
#endif

        /*
        **	A damaged unit has a reduced speed.
        */
        if ((Class->MaxStrength >> 1) > Strength) {
            speed -= (speed >> 2); // Three quarters speed.
        }
        if ((speed != Speed) /* || !SpeedAdd*/) {
            Set_Speed(speed); // Full speed.
        }

        /*
        **	Adjust speed depending on distance to ultimate movement target. The
        **	further away the target is, the faster the vehicle will travel.
        */
        int dist = Distance(NavCom);
        if (dist < 0x0200) {
            speed = Fixed_To_Cardinal(speed, 0x00A0);
        } else {
            if (dist < 0x0700) {
                speed = Fixed_To_Cardinal(speed, 0x00D0);
            }
        }

        /*
        **	Reserve the destination cell so that it won't become
        **	occupied AS this unit is moving into it.
        */
        if (cando != MOVE_OK) {
            Path[0] = FACING_NONE; // Path is blocked!
            TrackNumber = -1;
            dest = 0;
        } else {

            Overrun_Square(Coord_Cell(dest), true);

            /*
            **	Determine which track to use (based on recorded path).
            */
            FacingType nextface = Path[1];
            if (nextface == FACING_NONE)
                nextface = facing;

            IsOnShortTrack = false;
            TrackNumber = facing * FACING_COUNT + nextface;
            if (TrackControl[TrackNumber].Track == 0) {
                Path[0] = FACING_NONE;
                TrackNumber = -1;
                return (true);
            } else {
                if (TrackControl[TrackNumber].Flag & F_D) {
                    /*
                    **	If the middle cell of a two cell track contains a crate,
                    **	the check for goodies before movement starts.
                    */
                    if (!Map[destcell].Goodie_Check(this)) {
                        cando = MOVE_NO;
                    } else {

                        dest = Adjacent_Cell(dest, nextface);
                        destcell = Coord_Cell(dest);
                        cando = Can_Enter_Cell(destcell);
                    }

                    if (cando != MOVE_OK) {

                        /*
                        **	If a temporary friendly object is blocking the path, then cause it to
                        **	get out of the way.
                        */
                        if (cando == MOVE_TEMP) {
                            bool old = Special.IsScatter;
                            Special.IsScatter = true;
                            Map[destcell].Incoming(0, true);
                            Special.IsScatter = old;
                        }

                        /*
                        **	If a cloaked object is blocking, then shimmer the cell.
                        */
                        if (cando == MOVE_CLOAK) {
                            Map[destcell].Shimmer();
                        }

                        Path[0] = FACING_NONE; // Path is blocked!
                        TrackNumber = -1;
                        dest = 0;
                        if (cando == MOVE_DESTROYABLE) {

                            if (Map[destcell].Cell_Object()) {
                                if (!House->Is_Ally(Map[destcell].Cell_Object())) {
                                    Override_Mission(
                                        MISSION_ATTACK, Map[destcell].Cell_Object()->As_Target(), TARGET_NONE);
                                }
                            } else {
                                if (Map[destcell].Overlay != OVERLAY_NONE
                                    && OverlayTypeClass::As_Reference(Map[destcell].Overlay).IsWall) {
                                    Override_Mission(MISSION_ATTACK, ::As_Target(destcell), TARGET_NONE);
                                }
                            }
                            IsNewNavCom = false;
                            TrackIndex = 0;
                            return (true);
                        }
                    } else {
                        memmove(&Path[0], &Path[2], CONQUER_PATH_MAX - 2);
                        Path[CONQUER_PATH_MAX - 2] = FACING_NONE;
                        IsPlanningToLook = true;
                    }
                } else {
                    memmove(&Path[0], &Path[1], CONQUER_PATH_MAX - 1);
                }
                Path[CONQUER_PATH_MAX - 1] = FACING_NONE;
            }
        }

        IsNewNavCom = false;
        TrackIndex = 0;
        if (!Start_Driver(dest)) {
            TrackNumber = -1;
            Path[0] = FACING_NONE;
            Set_Speed(0);
        }
    }
    return (false);
}

/***********************************************************************************************
 * DriveClass::AI -- Processes unit movement and rotation.                                     *
 *                                                                                             *
 *    This routine is used to process unit movement and rotation. It                           *
 *    functions autonomously from the script system. Thus, once a unit                         *
 *    is give rotation command or movement path, it will follow this                           *
 *    until specifically instructed to stop. The advantage of this                             *
 *    method is that it allows smooth movement of units, faster game                           *
 *    execution, and reduced script complexity (since actual movement                          *
 *    dynamics need not be controlled directly by the scripts).                                *
 *                                                                                             *
 * INPUT:   none                                                                               *
 *                                                                                             *
 * OUTPUT:  none                                                                               *
 *                                                                                             *
 * WARNINGS:   This routine relies on the process control bits for the                         *
 *             specified unit (for speed reasons). Thus, only setting                          *
 *             movement, rotation, or path list will the unit perform                          *
 *             any physics.                                                                    *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   09/26/1993 JLB : Created.                                                                 *
 *   04/15/1994 JLB : Converted to member function.                                            *
 *=============================================================================================*/
void DriveClass::AI(void)
{
    FootClass::AI();

    /*
    **	If the unit is following a track, then continue
    **	to do so -- mindlessly.
    */
    if (TrackNumber != -1) {

        /*
        **	Perform the movement accumulation.
        */
        While_Moving();
        if (!IsActive)
            return;
        if (TrackNumber == -1 && (Target_Legal(NavCom) || Path[0] != FACING_NONE)) {
            Start_Of_Move();
            While_Moving();
            if (!IsActive)
                return;
        }

    } else {

        /*
        **	For tracked units that are rotating in place, perform the rotation now.
        */
        if ((Class->Speed == SPEED_FLOAT || Class->Speed == SPEED_HOVER || Class->Speed == SPEED_TRACK
             || (Class->Speed == SPEED_WHEEL && !Special.IsThreePoint))
            && PrimaryFacing.Is_Rotating()) {
            if (PrimaryFacing.Rotation_Adjust((int)Class->ROT * House->GroundspeedBias)) {
                Mark(MARK_CHANGE);
            }
            if (!IsRotating) {
                Per_Cell_Process(true);
                if (!IsActive)
                    return;
            }

        } else {

            /*
            **	The unit has no track to follow, but if there
            **	is a navigation target or a remaining path,
            **	then start on a new track.
            */
            if (Mission != MISSION_GUARD || NavCom != TARGET_NONE) {
                if (Target_Legal(NavCom) || Path[0] != FACING_NONE) {
                    Start_Of_Move();
                    While_Moving();
                    if (!IsActive)
                        return;
                } else {
                    Stop_Driver();
                }
            }
        }
    }
}

/***********************************************************************************************
 * DriveClass::Fixup_Path -- Adds smooth start path to normal movement path.                   *
 *                                                                                             *
 *    This routine modifies the path of the specified unit so that it                          *
 *    will not start out with a rotation. This is necessary for those                          *
 *    vehicles that have difficulty with rotating in place. Typically,                         *
 *    this includes wheeled vehicles.                                                          *
 *                                                                                             *
 * INPUT:   unit  -- Pointer to the unit to adjust.                                            *
 *                                                                                             *
 *          path  -- Pointer to path structure.                                                *
 *                                                                                             *
 * OUTPUT:  none                                                                               *
 *                                                                                             *
 * WARNINGS:   Only units that require a fixup get modified. The                               *
 *             modification only occurs, if there is a legal path to                           *
 *             do so.                                                                          *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   04/03/1994 JLB : Created.                                                                 *
 *   04/06/1994 JLB : Uses path structure.                                                     *
 *   04/10/1994 JLB : Diagonal smooth turn added.                                              *
 *   04/15/1994 JLB : Converted to member function.                                            *
 *=============================================================================================*/
void DriveClass::Fixup_Path(PathType* path)
{
    FacingType stage[6] = {FACING_N, FACING_N, FACING_N, FACING_N, FACING_N, FACING_N}; // Prefix path elements.
    int facediff; // The facing difference value (0..4 | 0..-4).
    static FacingType _path[4][6] = {
        {(FacingType)2, (FacingType)0, (FacingType)2, (FacingType)0, (FacingType)0, (FacingType)0},
        {(FacingType)3, (FacingType)0, (FacingType)2, (FacingType)2, (FacingType)0, (FacingType)0},
        {(FacingType)4, (FacingType)0, (FacingType)2, (FacingType)2, (FacingType)0, (FacingType)0},
        {(FacingType)4, (FacingType)0, (FacingType)2, (FacingType)2, (FacingType)1, (FacingType)0}};
    static FacingType _dpath[4][6] = {
        {(FacingType)0, (FacingType)0, (FacingType)0, (FacingType)0, (FacingType)0, (FacingType)0},
        {(FacingType)3, (FacingType)0, (FacingType)2, (FacingType)2, (FacingType)0, (FacingType)0},
        {(FacingType)4, (FacingType)0, (FacingType)2, (FacingType)2, (FacingType)1, (FacingType)0},
        {(FacingType)5, (FacingType)0, (FacingType)2, (FacingType)2, (FacingType)1, (FacingType)0}};

    int index;
    int counter;         // Path addition
    FacingType* ptr;     // Path list pointer.
    FacingType* ptr2;    // Copy of new path list pointer.
    FacingType nextpath; // Next path value.
    CELL cell;           // Working cell value.
    bool ok;

    /*
    **	Verify that the unit is valid and there is a path problem to resolve.
    */
    if (!path || path->Command[0] == FACING_NONE) {
        return;
    }

    /*
    **	Only wheeled vehicles need a path fixup -- to avoid 3 point turns.
    */
    if (!Special.IsThreePoint || Class->Speed != SPEED_WHEEL) {
        return;
    }

    /*
    **	If the original path starts in the same direction as the unit, then
    **	there is no problem to resolve -- abort.
    */
    facediff = PrimaryFacing.Difference((DirType)(path->Command[0] << 5)) >> 5;

    if (!facediff)
        return;

    if (Dir_Facing(PrimaryFacing) & FACING_NE) {
        ptr = &_dpath[(FacingType)ABS((int)facediff) - FACING_NE][1];         // Pointer to path adjust list.
        counter = (int)_dpath[(FacingType)ABS((int)facediff) - FACING_NE][0]; // Number of path adjusts.
    } else {
        ptr = &_path[(FacingType)ABS((int)facediff) - FACING_NE][1];         // Pointer to path adjust list.
        counter = (int)_path[(FacingType)ABS((int)facediff) - FACING_NE][0]; // Number of path adjusts.
    }
    ptr2 = ptr;

    ok = true;                            // Presume adjustment is all ok.
    cell = Coord_Cell(Coord);             // Starting cell.
    nextpath = Dir_Facing(PrimaryFacing); // Starting path.
    for (index = 0; index < counter; index++) {

        /*
        **	Determine next path element and add it to the
        **	working path list.
        */
        if (facediff > 0) {
            nextpath = nextpath + *ptr++;
        } else {
            nextpath = nextpath - *ptr++;
        }
        stage[index] = nextpath;
        cell = Adjacent_Cell(cell, nextpath);
        // cell = Coord_Cell(Adjacent_Cell(Cell_Coord(cell), nextpath));

        /*
        **	If it can't enter this cell, then abort the path
        **	building operation without adjusting the unit's
        **	path.
        */
        if (Can_Enter_Cell(cell, nextpath) != MOVE_OK) {
            ok = false;
            break;
        }
    }

    /*
    **	If veering to the left was not successful, then try veering
    **	to the right. This only makes sense if the vehicle is trying
    **	to turn 180 degrees.
    */
    if (!ok && ABS(facediff) == 4) {
        ptr = ptr2; // Pointer to path adjust list.
        facediff = -facediff;
        ok = true;                            // Presume adjustment is all ok.
        cell = Coord_Cell(Coord);             // Starting cell.
        nextpath = Dir_Facing(PrimaryFacing); // Starting path.
        for (index = 0; index < counter; index++) {

            /*
            **	Determine next path element and add it to the
            **	working path list.
            */
            if (facediff > 0) {
                nextpath = nextpath + *ptr++;
            } else {
                nextpath = nextpath - *ptr++;
            }
            stage[index] = nextpath;
            cell = Coord_Cell(Adjacent_Cell(Cell_Coord(cell), nextpath));

            /*
            **	If it can't enter this cell, then abort the path
            **	building operation without adjusting the unit's
            **	path.
            */
            if (Can_Enter_Cell(cell, nextpath) != MOVE_OK) {
                ok = false;
                break;
            }
        }
    }

    /*
    **	If a legal path addition was created, then install it in place
    **	of the first path value. The initial path entry is to be replaced
    **	with a sequence of path entries that create smooth turning.
    */
    if (ok) {
        if (path->Length <= 1) {
            movmem(&stage[0], path->Command, MAX(counter, 1));
            path->Length = counter;
        } else {

            /*
            **	Optimize the transition path step from the smooth turn
            **	first part as it joins with the rest of the normal
            **	path. The normal prefix path steps are NOT to be optimized.
            */
            if (counter) {
                counter--;
                path->Command[0] = stage[counter];
                Optimize_Moves(path, MOVE_OK);
            }

            /*
            **	If there is more than one prefix path element, then
            **	insert the rest now.
            */
            if (counter) {
                movmem(&path->Command[0], &path->Command[counter], 40 - counter);
                movmem(&stage[0], &path->Command[0], counter);
                path->Length += counter;
            }
        }
        path->Command[path->Length] = FACING_NONE;
    }
}

/***********************************************************************************************
 * DriveClass::Lay_Track -- Handles track laying logic for the unit.                           *
 *                                                                                             *
 *    This routine handles the track laying for the unit. This entails examining the unit's    *
 *    current location as well as the direction and whether this unit is allowed to lay        *
 *    tracks in the first place.                                                               *
 *                                                                                             *
 * INPUT:   none                                                                               *
 *                                                                                             *
 * OUTPUT:  none                                                                               *
 *                                                                                             *
 * WARNINGS:   none                                                                            *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   05/28/1994 JLB : Created.                                                                 *
 *=============================================================================================*/
void DriveClass::Lay_Track(void)
{
#ifdef NEVER
    static IconCommandType* _trackdirs[8] = {
        TrackN_S, TrackNE_SW, TrackE_W, TrackNW_SE, TrackN_S, TrackNE_SW, TrackE_W, TrackNW_SE};

    if (!(ClassF & CLASSF_TRACKS))
        return;

    Icon_Install(Coord_Cell(Coord), _trackdirs[Facing_To_8(BodyFacing)]);
#endif
}

/***********************************************************************************************
 * DriveClass::Mark_Track -- Marks the midpoint of the track as occupied.                      *
 *                                                                                             *
 *    This routine will ensure that the midpoint (if any) of the track that the unit is        *
 *    following, will be marked according to the mark type specified.                          *
 *                                                                                             *
 * INPUT:   headto   -- The head to coordinate.                                                *
 *                                                                                             *
 *          type     -- The type of marking to perform.                                        *
 *                                                                                             *
 * OUTPUT:  none                                                                               *
 *                                                                                             *
 * WARNINGS:   none                                                                            *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   07/30/1995 JLB : Created.                                                                 *
 *=============================================================================================*/
void DriveClass::Mark_Track(COORDINATE headto, MarkType type)
{
    int value;

    if (type == MARK_UP) {
        value = false;
    } else {
        value = true;
    }

    if (headto) {
        if (!IsOnShortTrack && TrackNumber != -1) {

            /*
            ** If we have not passed the per cell process point we need
            ** to deal with it.
            */
            int tracknum = TrackControl[TrackNumber].Track;
            if (tracknum) {
                TrackType const* ptr = RawTracks[tracknum - 1].Track;
                int cellidx = RawTracks[tracknum - 1].Cell;
                if (cellidx > -1) {
                    DirType dir = ptr[cellidx].Facing;

                    if (TrackIndex < cellidx && cellidx != -1) {
                        COORDINATE offset = Smooth_Turn(ptr[cellidx].Offset, &dir);
                        CELL cell = Coord_Cell(offset);
                        if ((unsigned)cell < MAP_CELL_TOTAL) {
                            Map[cell].Flag.Occupy.Vehicle = value;
                        }
                    }
                }
            }
        }
        CELL cell = Coord_Cell(headto);
        if ((unsigned)cell < MAP_CELL_TOTAL) {
            Map[cell].Flag.Occupy.Vehicle = value;
        }
    }
}

/***********************************************************************************************
 * DriveClass::Offload_Tiberium_Bail -- Offloads one Tiberium quantum from the object.         *
 *                                                                                             *
 *    This routine will offload one Tiberium packet/quantum/bail from the object. Multiple     *
 *    calls to this routine are needed in order to fully offload all Tiberium.                 *
 *                                                                                             *
 * INPUT:   none                                                                               *
 *                                                                                             *
 * OUTPUT:  Returns with the number of credits offloaded for the one call. If zero is returned,*
 *          then this indicates that all Tiberium has been offloaded.                          *
 *                                                                                             *
 * WARNINGS:   none                                                                            *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   07/19/1995 JLB : Created.                                                                 *
 *=============================================================================================*/
int DriveClass::Offload_Tiberium_Bail(void)
{
    if (Tiberium) {
        Tiberium--;
        if (House->IsHuman) {
            return (UnitTypeClass::FULL_LOAD_CREDITS / UnitTypeClass::STEP_COUNT); // 25 in debugger
        }

        // MBL 05.14.2020: AI harvested credits fix for multiplayer, since they are miscalculated, and it's noticed
        //
        // return(UnitTypeClass::FULL_LOAD_CREDITS+(UnitTypeClass::FULL_LOAD_CREDITS/3)/UnitTypeClass::STEP_COUNT); 708
        // in debugger
        //
        if (GameToPlay == GAME_NORMAL) // Non-multiplayer game, keep the original calculation; 708 in debugger
        {
            return (UnitTypeClass::FULL_LOAD_CREDITS
                    + (UnitTypeClass::FULL_LOAD_CREDITS / 3)
                          / UnitTypeClass::STEP_COUNT); // Original (708), wrong calcualation but preserving to not
                                                        // break missions
        } else // Multiplayer game, apply the 1/3 bonus credits correction, so not be as extreme; 33 in debugger
        {
            return ((UnitTypeClass::FULL_LOAD_CREDITS + (UnitTypeClass::FULL_LOAD_CREDITS / 3))
                    / UnitTypeClass::STEP_COUNT); // Corrected calculation
        }
    }
    return (0);
}

/***********************************************************************************************
 * DriveClass::Ok_To_Move -- Checks to see if this object can begin moving.                    *
 *                                                                                             *
 *    This routine is used to verify that this object is allowed to move. Some objects can     *
 *    be temporarily occupied and thus cannot move until the situation permits.                *
 *                                                                                             *
 * INPUT:   direction   -- The direction that movement would be desired.                       *
 *                                                                                             *
 * OUTPUT:  Can the unit move in the direction specified?                                      *
 *                                                                                             *
 * WARNINGS:   none                                                                            *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   07/29/1995 JLB : Created.                                                                 *
 *=============================================================================================*/
bool DriveClass::Ok_To_Move(DirType) const
{
    return true;
}

/***********************************************************************************************
 * DriveClass::Class_Of -- Fetches a reference to the class type for this object.              *
 *                                                                                             *
 *    This routine will fetch a reference to the TypeClass of this object.                     *
 *                                                                                             *
 * INPUT:   none                                                                               *
 *                                                                                             *
 * OUTPUT:  Returns with reference to the type class of this object.                           *
 *                                                                                             *
 * WARNINGS:   none                                                                            *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   07/29/1995 JLB : Created.                                                                 *
 *=============================================================================================*/
ObjectTypeClass const& DriveClass::Class_Of(void) const
{
    return *Class;
}

/***************************************************************************
**	Smooth turn track tables. These are coordinate offsets from the center
**	of the destination cell. These are the raw tracks that are modified
**	by negating the X and Y portions as necessary. Also for reverse travelling
**	direction, the track list can be processed backward.
**
**	Track 1 = N
**	Track 2 = NE
**	Track 3 = N->NE 45 deg (double path consumption)
**	Track 4 = N->E 90 deg (double path consumption)
**	Track 5 = NE->SE 90 deg (double path consumption)
** Track 6 = NE->N 45 deg (double path consumption)
**	Track 7 = N->NE (facing change only)
**	Track 8 = NE->E (facing change only)
**	Track 9 = N->E (facing change only)
**	Track 10= NE->SE (facing change only)
**	Track 11= back up into refinery
**	Track 12= drive out of refinery
*/
//#pragma warn -ias
DriveClass::TrackType const DriveClass::Track1[24] = {
    {XY_Delta(0, 245), (DirType)0}, {XY_Delta(0, 234), (DirType)0}, {XY_Delta(0, 223), (DirType)0},
    {XY_Delta(0, 212), (DirType)0}, {XY_Delta(0, 201), (DirType)0}, {XY_Delta(0, 190), (DirType)0},
    {XY_Delta(0, 179), (DirType)0}, {XY_Delta(0, 168), (DirType)0}, {XY_Delta(0, 157), (DirType)0},
    {XY_Delta(0, 146), (DirType)0}, {XY_Delta(0, 135), (DirType)0}, {XY_Delta(0, 124), (DirType)0}, // Track jump check here.
    {XY_Delta(0, 113), (DirType)0}, {XY_Delta(0, 102), (DirType)0}, {XY_Delta(0, 91), (DirType)0},
    {XY_Delta(0, 80), (DirType)0}, {XY_Delta(0, 69), (DirType)0}, {XY_Delta(0, 58), (DirType)0},
    {XY_Delta(0, 47), (DirType)0}, {XY_Delta(0, 36), (DirType)0}, {XY_Delta(0, 25), (DirType)0},
    {XY_Delta(0, 14), (DirType)0}, {XY_Delta(0, 3), (DirType)0}, {XY_Delta(0, 0), (DirType)0}};

DriveClass::TrackType const DriveClass::Track2[] = {
    {XY_Delta(-248, 248), (DirType)32}, {XY_Delta(-240, 240), (DirType)32}, {XY_Delta(-232, 232), (DirType)32},
    {XY_Delta(-224, 224), (DirType)32}, {XY_Delta(-216, 216), (DirType)32}, {XY_Delta(-208, 208), (DirType)32},
    {XY_Delta(-200, 200), (DirType)32}, {XY_Delta(-192, 192), (DirType)32}, {XY_Delta(-184, 184), (DirType)32},
    {XY_Delta(-176, 176), (DirType)32}, {XY_Delta(-168, 168), (DirType)32}, {XY_Delta(-160, 160), (DirType)32},
    {XY_Delta(-152, 152), (DirType)32}, {XY_Delta(-144, 144), (DirType)32}, {XY_Delta(-136, 136), (DirType)32},
    {XY_Delta(-128, 128), (DirType)32}, // Track jump check here.
    {XY_Delta(-120, 120), (DirType)32}, {XY_Delta(-112, 112), (DirType)32}, {XY_Delta(-104, 104), (DirType)32},
    {XY_Delta(-96, 96), (DirType)32}, {XY_Delta(-88, 88), (DirType)32}, {XY_Delta(-80, 80), (DirType)32},
    {XY_Delta(-72, 72), (DirType)32}, {XY_Delta(-64, 64), (DirType)32}, {XY_Delta(-56, 56), (DirType)32},
    {XY_Delta(-48, 48), (DirType)32}, {XY_Delta(-40, 40), (DirType)32}, {XY_Delta(-32, 32), (DirType)32},
    {XY_Delta(-24, 24), (DirType)32}, {XY_Delta(-16, 16), (DirType)32}, {XY_Delta(-8, 8), (DirType)32},
    {XY_Delta(0, 0), (DirType)32}};

DriveClass::TrackType const DriveClass::Track3[] = {
    {XY_Delta(-256, 501), (DirType)0},  {XY_Delta(-256, 490), (DirType)0},  {XY_Delta(-256, 479), (DirType)0},  {XY_Delta(-256, 468), (DirType)0},
    {XY_Delta(-256, 457), (DirType)0},  {XY_Delta(-256, 446), (DirType)0},  {XY_Delta(-256, 435), (DirType)0},  {XY_Delta(-256, 424), (DirType)0},
    {XY_Delta(-256, 413), (DirType)0},  {XY_Delta(-256, 402), (DirType)0},  {XY_Delta(-256, 391), (DirType)0},  {XY_Delta(-256, 384), (DirType)0},
    {XY_Delta(-256, 373), (DirType)0}, // Jump entry point here.
    {XY_Delta(-256, 363), (DirType)0},  {XY_Delta(-254, 352), (DirType)1},  {XY_Delta(-252, 341), (DirType)3},  {XY_Delta(-250, 332), (DirType)4},
    {XY_Delta(-248, 321), (DirType)5},  {XY_Delta(-245, 311), (DirType)7},  {XY_Delta(-241, 302), (DirType)8},  {XY_Delta(-237, 292), (DirType)9},
    {XY_Delta(-233, 282), (DirType)11}, {XY_Delta(-229, 272), (DirType)12}, {XY_Delta(-225, 263), (DirType)13}, // Center cell processing here.
    {XY_Delta(-220, 252), (DirType)15}, {XY_Delta(-216, 243), (DirType)16}, {XY_Delta(-212, 236), (DirType)17}, {XY_Delta(-206, 224), (DirType)19},
    {XY_Delta(-202, 215), (DirType)20}, {XY_Delta(-195, 207), (DirType)21}, {XY_Delta(-190, 198), (DirType)23}, {XY_Delta(-183, 186), (DirType)24},
    {XY_Delta(-179, 176), (DirType)25}, {XY_Delta(-168, 168), (DirType)27}, {XY_Delta(-160, 160), (DirType)28}, {XY_Delta(-152, 152), (DirType)29},
    {XY_Delta(-144, 144), (DirType)31}, {XY_Delta(-136, 136), (DirType)32}, {XY_Delta(-128, 128), (DirType)32}, // Track jump check here.
    {XY_Delta(-120, 120), (DirType)32}, {XY_Delta(-112, 112), (DirType)32}, {XY_Delta(-104, 104), (DirType)32}, {XY_Delta(-96, 96), (DirType)32},
    {XY_Delta(-88, 88), (DirType)32}, {XY_Delta(-80, 80), (DirType)32}, {XY_Delta(-72, 72), (DirType)32}, {XY_Delta(-64, 64), (DirType)32},
    {XY_Delta(-56, 56), (DirType)32}, {XY_Delta(-48, 48), (DirType)32}, {XY_Delta(-40, 40), (DirType)32}, {XY_Delta(-32, 32), (DirType)32},
    {XY_Delta(-24, 24), (DirType)32}, {XY_Delta(-16, 16), (DirType)32}, {XY_Delta(-8, 8), (DirType)32}, {XY_Delta(0, 0), (DirType)32}};

DriveClass::TrackType const DriveClass::Track4[] = {
    {XY_Delta(-256, 245), (DirType)0},  {XY_Delta(-256, 235), (DirType)0},  {XY_Delta(-256, 224), (DirType)0},
    {XY_Delta(-256, 213), (DirType)0},  {XY_Delta(-255, 203), (DirType)0},  {XY_Delta(-253, 192), (DirType)0},
    {XY_Delta(-251, 181), (DirType)1},  {XY_Delta(-249, 171), (DirType)1},  {XY_Delta(-246, 160), (DirType)2},
    {XY_Delta(-243, 149), (DirType)3},  {XY_Delta(-240, 139), (DirType)4},  {XY_Delta(-236, 128), (DirType)5}, // Track entry here.
    {XY_Delta(-232, 117), (DirType)8},  {XY_Delta(-228, 109), (DirType)12}, {XY_Delta(-222, 99), (DirType)16},
    {XY_Delta(-219, 90), (DirType)20}, {XY_Delta(-213, 82), (DirType)23}, {XY_Delta(-206, 72), (DirType)27},
    {XY_Delta(-201, 64), (DirType)32}, {XY_Delta(-195, 56), (DirType)36}, {XY_Delta(-186, 48), (DirType)39},
    {XY_Delta(-177, 43), (DirType)43}, {XY_Delta(-168, 36), (DirType)47}, {XY_Delta(-160, 32), (DirType)51},
    {XY_Delta(-147, 27), (DirType)54}, {XY_Delta(-135, 23), (DirType)57}, {XY_Delta(-126, 20), (DirType)60}, // Track jump here.
    {XY_Delta(-113, 17), (DirType)62}, {XY_Delta(-104, 13), (DirType)63}, {XY_Delta(-94, 9), (DirType)64},
    {XY_Delta(-84, 6), (DirType)64}, {XY_Delta(-75, 4), (DirType)66}, {XY_Delta(-64, 3), (DirType)64},
    {XY_Delta(-53, 2), (DirType)64}, {XY_Delta(-43, 1), (DirType)64}, {XY_Delta(-32, 0), (DirType)64},
    {XY_Delta(-21, 0), (DirType)64}, {XY_Delta(-11, 0), (DirType)64}, {XY_Delta(0, 0), (DirType)64}};

DriveClass::TrackType const DriveClass::Track5[] = {
    {XY_Delta(-504, -8), (DirType)32}, {XY_Delta(-496, -16), (DirType)32}, {XY_Delta(-488, -24), (DirType)32},
    {XY_Delta(-480, -32), (DirType)32}, {XY_Delta(-472, -40), (DirType)32}, {XY_Delta(-464, -48), (DirType)32},
    {XY_Delta(-456, -56), (DirType)32}, {XY_Delta(-448, -64), (DirType)32}, {XY_Delta(-440, -72), (DirType)32},
    {XY_Delta(-432, -80), (DirType)32}, {XY_Delta(-424, -88), (DirType)32}, {XY_Delta(-416, -96), (DirType)32},
    {XY_Delta(-408, -104), (DirType)32}, {XY_Delta(-400, -112), (DirType)32}, {XY_Delta(-392, -120), (DirType)32},
    {XY_Delta(-384, -128), (DirType)32}, // Track entry here.
    {XY_Delta(-376, -136), (DirType)32}, {XY_Delta(-368, -143), (DirType)32}, {XY_Delta(-361, -150), (DirType)32},
    {XY_Delta(-353, -158), (DirType)32}, {XY_Delta(-344, -166), (DirType)32}, {XY_Delta(-336, -173), (DirType)35},
    {XY_Delta(-329, -181), (DirType)38}, {XY_Delta(-322, -188), (DirType)41}, {XY_Delta(-316, -194), (DirType)44},
    {XY_Delta(-306, -199), (DirType)47}, {XY_Delta(-296, -204), (DirType)50}, {XY_Delta(-288, -208), (DirType)53},
    {XY_Delta(-277, -211), (DirType)56}, {XY_Delta(-267, -212), (DirType)59}, {XY_Delta(-256, -213), (DirType)62},
    {XY_Delta(-245, -212), (DirType)66}, {XY_Delta(-235, -211), (DirType)69}, {XY_Delta(-225, -208), (DirType)72},
    {XY_Delta(-216, -204), (DirType)75}, {XY_Delta(-208, -199), (DirType)78}, {XY_Delta(-198, -194), (DirType)81},
    {XY_Delta(-188, -188), (DirType)84}, {XY_Delta(-181, -181), (DirType)87}, {XY_Delta(-176, -173), (DirType)90},
    {XY_Delta(-168, -166), (DirType)93}, {XY_Delta(-160, -158), (DirType)96}, {XY_Delta(-152, -150), (DirType)96},
    {XY_Delta(-144, -143), (DirType)96}, {XY_Delta(-136, -136), (DirType)96}, {XY_Delta(-128, -128), (DirType)96}, // Track jump check here.
    {XY_Delta(-120, -120), (DirType)96}, {XY_Delta(-112, -112), (DirType)96}, {XY_Delta(-104, -104), (DirType)96},
    {XY_Delta(-96, -96), (DirType)96}, {XY_Delta(-88, -88), (DirType)96}, {XY_Delta(-80, -80), (DirType)96},
    {XY_Delta(-72, -72), (DirType)96}, {XY_Delta(-64, -64), (DirType)96}, {XY_Delta(-56, -56), (DirType)96},
    {XY_Delta(-48, -48), (DirType)96}, {XY_Delta(-40, -40), (DirType)96}, {XY_Delta(-32, -32), (DirType)96},
    {XY_Delta(-24, -24), (DirType)96}, {XY_Delta(-16, -16), (DirType)96}, {XY_Delta(-8, -8), (DirType)96},
    {XY_Delta(0, 0), (DirType)96}};

DriveClass::TrackType const DriveClass::Track6[] = {
    {XY_Delta(-512, 256), (DirType)32}, {XY_Delta(-504, 248), (DirType)32}, {XY_Delta(-496, 240), (DirType)32},
    {XY_Delta(-488, 232), (DirType)32}, {XY_Delta(-480, 224), (DirType)32}, {XY_Delta(-472, 216), (DirType)32},
    {XY_Delta(-464, 208), (DirType)32}, {XY_Delta(-456, 200), (DirType)32}, {XY_Delta(-448, 192), (DirType)32},
    {XY_Delta(-440, 184), (DirType)32}, {XY_Delta(-432, 176), (DirType)32}, {XY_Delta(-424, 168), (DirType)32},
    {XY_Delta(-416, 160), (DirType)32}, {XY_Delta(-408, 152), (DirType)32}, {XY_Delta(-400, 144), (DirType)32},
    {XY_Delta(-392, 136), (DirType)32}, {XY_Delta(-384, 128), (DirType)32}, // Jump entry point here.
    {XY_Delta(-376, 120), (DirType)32}, {XY_Delta(-368, 112), (DirType)32}, {XY_Delta(-360, 104), (DirType)32},
    {XY_Delta(-352, 96), (DirType)32}, {XY_Delta(-344, 88), (DirType)32}, {XY_Delta(-338, 85), (DirType)32},
    {XY_Delta(-328, 78), (DirType)35}, {XY_Delta(-320, 72), (DirType)37}, {XY_Delta(-311, 66), (DirType)40},
    {XY_Delta(-302, 59), (DirType)43}, {XY_Delta(-294, 55), (DirType)45}, {XY_Delta(-285, 50), (DirType)48},
    {XY_Delta(-277, 43), (DirType)51}, {XY_Delta(-267, 38), (DirType)53}, {XY_Delta(-258, 34), (DirType)56},
    {XY_Delta(-248, 28), (DirType)59}, {XY_Delta(-238, 25), (DirType)61}, {XY_Delta(-229, 21), (DirType)64},
    {XY_Delta(-218, 17), (DirType)64}, {XY_Delta(-208, 14), (DirType)64}, {XY_Delta(-199, 11), (DirType)64},
    {XY_Delta(-189, 9), (DirType)64}, {XY_Delta(-178, 7), (DirType)64}, {XY_Delta(-169, 5), (DirType)64},
    {XY_Delta(-158, 3), (DirType)64}, {XY_Delta(-147, 1), (DirType)64}, {XY_Delta(-137, 0), (DirType)64},
    {XY_Delta(-128, 0), (DirType)64}, // Track jump check here.
    {XY_Delta(-117, 0), (DirType)64}, {XY_Delta(-107, 0), (DirType)64}, {XY_Delta(-96, 0), (DirType)64},
    {XY_Delta(-85, 0), (DirType)64}, {XY_Delta(-75, 0), (DirType)64}, {XY_Delta(-64, 0), (DirType)64},
    {XY_Delta(-53, 0), (DirType)64}, {XY_Delta(-43, 0), (DirType)64}, {XY_Delta(-32, 0), (DirType)64},
    {XY_Delta(-21, 0), (DirType)64}, {XY_Delta(-11, 0), (DirType)64}, {XY_Delta(0, 0), (DirType)64}};

DriveClass::TrackType const DriveClass::Track7[] = {
    {XY_Delta(-1, 6), (DirType)0},  {XY_Delta(-2, 12), (DirType)4},  {XY_Delta(-4, 17), (DirType)8},  {XY_Delta(-6, 24), (DirType)12},
    {XY_Delta(-10, 31), (DirType)16}, {XY_Delta(-13, 36), (DirType)19}, {XY_Delta(-16, 43), (DirType)22}, {XY_Delta(-3, 48), (DirType)23},
    {XY_Delta(-21, 53), (DirType)24}, {XY_Delta(-24, 56), (DirType)25}, {XY_Delta(-26, 60), (DirType)26}, {XY_Delta(-29, 64), (DirType)27},
    {XY_Delta(-32, 67), (DirType)28}, {XY_Delta(-35, 70), (DirType)29}, {XY_Delta(-33, 67), (DirType)30}, {XY_Delta(-31, 64), (DirType)30},
    {XY_Delta(-29, 60), (DirType)30}, {XY_Delta(-27, 56), (DirType)30}, {XY_Delta(-25, 53), (DirType)31}, {XY_Delta(-23, 48), (DirType)31},
    {XY_Delta(-21, 43), (DirType)31}, {XY_Delta(-19, 36), (DirType)31}, {XY_Delta(-15, 31), (DirType)31}, {XY_Delta(-12, 24), (DirType)32},
    {XY_Delta(-9, 17), (DirType)32}, {XY_Delta(-6, 12), (DirType)32}, {XY_Delta(-3, 6), (DirType)32}, {XY_Delta(0, 0), (DirType)32}};

DriveClass::TrackType const DriveClass::Track8[] = {
    {XY_Delta(-4, 3), (DirType)32}, {XY_Delta(-9, 6), (DirType)36}, {XY_Delta(-15, 10), (DirType)40}, {XY_Delta(-21, 12), (DirType)44},
    {XY_Delta(-28, 13), (DirType)46}, {XY_Delta(-36, 14), (DirType)48}, {XY_Delta(-43, 15), (DirType)50}, {XY_Delta(-48, 16), (DirType)52},
    {XY_Delta(-55, 17), (DirType)54}, {XY_Delta(-62, 18), (DirType)56}, {XY_Delta(-64, 17), (DirType)58}, {XY_Delta(-62, 16), (DirType)60},
    {XY_Delta(-55, 14), (DirType)62}, {XY_Delta(-49, 12), (DirType)64}, {XY_Delta(-43, 10), (DirType)64}, {XY_Delta(-38, 8), (DirType)64},
    {XY_Delta(-30, 6), (DirType)64}, {XY_Delta(-23, 4), (DirType)64}, {XY_Delta(-17, 2), (DirType)64}, {XY_Delta(-11, 1), (DirType)64},
    {XY_Delta(-7, 0), (DirType)64}, {XY_Delta(0, 0), (DirType)64}};

DriveClass::TrackType const DriveClass::Track9[] = {
    {XY_Delta(2, -11), (DirType)0},  {XY_Delta(4, -21), (DirType)2},  {XY_Delta(6, -32), (DirType)4},  {XY_Delta(9, -43), (DirType)6},
    {XY_Delta(12, -50), (DirType)9},  {XY_Delta(15, -56), (DirType)11}, {XY_Delta(18, -64), (DirType)13}, {XY_Delta(21, -72), (DirType)16},
    {XY_Delta(18, -64), (DirType)18}, {XY_Delta(14, -56), (DirType)20}, {XY_Delta(10, -50), (DirType)22}, {XY_Delta(4, -43), (DirType)24},
    {XY_Delta(0, -34), (DirType)26}, {XY_Delta(-8, -23), (DirType)28}, {XY_Delta(-14, -18), (DirType)30}, {XY_Delta(-21, -11), (DirType)32},
    {XY_Delta(-31, -3), (DirType)34}, {XY_Delta(-40, 2), (DirType)36}, {XY_Delta(-46, 7), (DirType)39}, {XY_Delta(-53, 11), (DirType)41},
    {XY_Delta(-59, 16), (DirType)43}, {XY_Delta(-66, 19), (DirType)45}, {XY_Delta(-73, 21), (DirType)48}, {XY_Delta(-66, 19), (DirType)50},
    {XY_Delta(-59, 17), (DirType)52}, {XY_Delta(-52, 11), (DirType)54}, {XY_Delta(-44, 8), (DirType)56}, {XY_Delta(-33, 5), (DirType)58},
    {XY_Delta(-21, 3), (DirType)62}, {XY_Delta(-11, 1), (DirType)64}, {XY_Delta(0, 0), (DirType)64}};

DriveClass::TrackType const DriveClass::Track10[] = {
    {XY_Delta(11, -10), (DirType)32}, {XY_Delta(21, -16), (DirType)37}, {XY_Delta(32, -21), (DirType)42}, {XY_Delta(43, -23), (DirType)47},
    {XY_Delta(50, -27), (DirType)52}, {XY_Delta(56, -29), (DirType)57}, {XY_Delta(64, -32), (DirType)60}, {XY_Delta(56, -30), (DirType)62},
    {XY_Delta(50, -28), (DirType)64}, {XY_Delta(42, -27), (DirType)68}, {XY_Delta(30, -26), (DirType)70}, {XY_Delta(21, -25), (DirType)72},
    {XY_Delta(11, -24), (DirType)74}, {XY_Delta(0, -23), (DirType)76}, {XY_Delta(-11, -24), (DirType)78}, {XY_Delta(-21, -25), (DirType)80},
    {XY_Delta(-32, -26), (DirType)82}, {XY_Delta(-43, -27), (DirType)84}, {XY_Delta(-50, -28), (DirType)86}, {XY_Delta(-59, -30), (DirType)88},
    {XY_Delta(-64, -32), (DirType)90}, {XY_Delta(-59, -29), (DirType)92}, {XY_Delta(-50, -27), (DirType)94}, {XY_Delta(-43, -23), (DirType)95},
    {XY_Delta(-32, -21), (DirType)96}, {XY_Delta(-21, -16), (DirType)96}, {XY_Delta(-11, -10), (DirType)96}, {XY_Delta(0, 0), (DirType)96}};

DriveClass::TrackType const DriveClass::Track11[] = {{XY_Delta(0, 256), DIR_SW},
                                                     {XY_Delta(8, 243), DIR_SW},
                                                     {XY_Delta(16, 229), DIR_SW_X1},
                                                     {XY_Delta(24, 214), DIR_SW_X1},
                                                     {XY_Delta(32, 200), DIR_SW_X1},
                                                     {XY_Delta(40, 185), DIR_SW_X1},
                                                     {XY_Delta(48, 171), DIR_SW_X2},
                                                     {XY_Delta(56, 156), DIR_SW_X2},
                                                     {XY_Delta(64, 141), DIR_SW_X2},
                                                     {XY_Delta(72, 127), DIR_SW_X2},
                                                     {XY_Delta(80, 113), DIR_SW_X2},
                                                     {XY_Delta(88, 100), DIR_SW_X2},
                                                     {XY_Delta(96, 85), DIR_SW_X2},

                                                     {XY_Delta(0, 0), DIR_SW_X2}};

DriveClass::TrackType const DriveClass::Track12[] = {{XY_Delta(96, -171), DIR_SW_X2},
                                                     {XY_Delta(88, -156), DIR_SW_X2},
                                                     {XY_Delta(80, -143), DIR_SW_X2},
                                                     {XY_Delta(72, -129), DIR_SW_X2},
                                                     {XY_Delta(64, -115), DIR_SW_X2},
                                                     {XY_Delta(56, -100), DIR_SW_X2},
                                                     {XY_Delta(48, -85), DIR_SW_X2},
                                                     {XY_Delta(40, -71), DIR_SW_X1},
                                                     {XY_Delta(32, -56), DIR_SW_X1},
                                                     {XY_Delta(24, -42), DIR_SW_X1},
                                                     {XY_Delta(16, -27), DIR_SW_X1},
                                                     {XY_Delta(8, -13), DIR_SW},

                                                     {XY_Delta(0, 0), DIR_SW}};

/*
**	Drive out of weapon's factory.
*/
DriveClass::TrackType const DriveClass::Track13[] = {{XYP_COORD(10, -21), (DirType)(DIR_SW - 10)},
                                                     {XYP_COORD(10, -21), (DirType)(DIR_SW - 10)},
                                                     {XYP_COORD(10, -20), (DirType)(DIR_SW - 10)},
                                                     {XYP_COORD(10, -20), (DirType)(DIR_SW - 10)},
                                                     {XYP_COORD(9, -18), (DirType)(DIR_SW - 10)},
                                                     {XYP_COORD(9, -18), (DirType)(DIR_SW - 10)},
                                                     {XYP_COORD(9, -17), (DirType)(DIR_SW - 10)},
                                                     {XYP_COORD(8, -16), (DirType)(DIR_SW - 10)},
                                                     {XYP_COORD(8, -15), (DirType)(DIR_SW - 10)},
                                                     {XYP_COORD(7, -14), (DirType)(DIR_SW - 10)},
                                                     {XYP_COORD(7, -13), (DirType)(DIR_SW - 10)},
                                                     {XYP_COORD(6, -12), (DirType)(DIR_SW - 10)},
                                                     {XYP_COORD(6, -11), (DirType)(DIR_SW - 10)},
                                                     {XYP_COORD(5, -10), (DirType)(DIR_SW - 10)},
                                                     {XYP_COORD(5, -9), (DirType)(DIR_SW - 10)},
                                                     {XYP_COORD(4, -8), (DirType)(DIR_SW - 10)},
                                                     {XYP_COORD(4, -7), (DirType)(DIR_SW - 10)},
                                                     {XYP_COORD(3, -6), (DirType)(DIR_SW - 10)},
                                                     {XYP_COORD(3, -5), (DirType)(DIR_SW - 9)},
                                                     {XYP_COORD(2, -4), (DirType)(DIR_SW - 7)},
                                                     {XYP_COORD(2, -3), (DirType)(DIR_SW - 5)},
                                                     {XYP_COORD(1, -2), (DirType)(DIR_SW - 3)},
                                                     {XYP_COORD(1, -1), (DirType)(DIR_SW - 1)},

                                                     {XY_Delta(0, 0), DIR_SW}};

/*
**	There are a limited basic number of tracks that a vehicle can follow. These
**	are they. Each track can be interpreted differently but this is controlled
**	by the TrackControl structure elaborated elsewhere.
*/
DriveClass::RawTrackType const DriveClass::RawTracks[13] = {{Track1, -1, 0, -1},
                                                            {Track2, -1, 0, -1},
                                                            {Track3, 37, 12, 22},
                                                            {Track4, 26, 11, 19},
                                                            {Track5, 45, 15, 31},
                                                            {Track6, 44, 16, 27},
                                                            {Track7, -1, 0, -1},
                                                            {Track8, -1, 0, -1},
                                                            {Track9, -1, 0, -1},
                                                            {Track10, -1, 0, -1},
                                                            {Track11, -1, 0, -1},
                                                            {Track12, -1, 0, -1},
                                                            {Track13, -1, 0, -1}};

/***************************************************************************
**	Smooth turning control table. Given two directions in a path list, this
**	table determines which track to use and what modifying operations need
**	be performed on the track data.
*/
DriveClass::TurnTrackType const DriveClass::TrackControl[67] = {
    {1, 0, DIR_N, F_},                                                      //	0-0
    {3, 7, DIR_NE, F_D},                                                    //	0-1 (raw chart)
    {4, 9, DIR_E, F_D},                                                     //	0-2 (raw chart)
    {0, 0, DIR_SE, F_},                                                     //	0-3 !
    {0, 0, DIR_S, F_},                                                      //	0-4 !
    {0, 0, DIR_SW, F_},                                                     //	0-5 !
    {4, 9, DIR_W, (DriveClass::TrackControlType)(F_X | F_D)},               //	0-6
    {3, 7, DIR_NW, (DriveClass::TrackControlType)(F_X | F_D)},              //	0-7
    {6, 8, DIR_N, (DriveClass::TrackControlType)(F_T | F_X | F_Y | F_D)},   //	1-0
    {2, 0, DIR_NE, F_},                                                     //	1-1 (raw chart)
    {6, 8, DIR_E, F_D},                                                     //	1-2 (raw chart)
    {5, 10, DIR_SE, F_D},                                                   //	1-3 (raw chart)
    {0, 0, DIR_S, F_},                                                      //	1-4 !
    {0, 0, DIR_SW, F_},                                                     //	1-5 !
    {0, 0, DIR_W, F_},                                                      //	1-6 !
    {5, 10, DIR_NW, (DriveClass::TrackControlType)(F_T | F_X | F_Y | F_D)}, //	1-7
    {4, 9, DIR_N, (DriveClass::TrackControlType)(F_T | F_X | F_Y | F_D)},   //	2-0
    {3, 7, DIR_NE, (DriveClass::TrackControlType)(F_T | F_X | F_Y | F_D)},  //	2-1
    {1, 0, DIR_E, (DriveClass::TrackControlType)(F_T | F_X)},               //	2-2
    {3, 7, DIR_SE, (DriveClass::TrackControlType)(F_T | F_X | F_D)},        //	2-3
    {4, 9, DIR_S, (DriveClass::TrackControlType)(F_T | F_X | F_D)},         //	2-4
    {0, 0, DIR_SW, F_},                                                     //	2-5 !
    {0, 0, DIR_W, F_},                                                      //	2-6 !
    {0, 0, DIR_NW, F_},                                                     //	2-7 !
    {0, 0, DIR_N, F_},                                                      //	3-0 !
    {5, 10, DIR_NE, (DriveClass::TrackControlType)(F_Y | F_D)},             //	3-1
    {6, 8, DIR_E, (DriveClass::TrackControlType)(F_Y | F_D)},               //	3-2
    {2, 0, DIR_SE, F_Y},                                                    //	3-3
    {6, 8, DIR_S, (DriveClass::TrackControlType)(F_T | F_X | F_D)},         //	3-4
    {5, 10, DIR_SW, (DriveClass::TrackControlType)(F_T | F_X | F_D)},       //	3-5
    {0, 0, DIR_W, F_},                                                      //	3-6 !
    {0, 0, DIR_NW, F_},                                                     //	3-7 !
    {0, 0, DIR_N, F_},                                                      //	4-0 !
    {0, 0, DIR_NE, F_},                                                     //	4-1 !
    {4, 9, DIR_E, (DriveClass::TrackControlType)(F_Y | F_D)},               //	4-2
    {3, 7, DIR_SE, (DriveClass::TrackControlType)(F_Y | F_D)},              //	4-3
    {1, 0, DIR_S, F_Y},                                                     //	4-4
    {3, 7, DIR_SW, (DriveClass::TrackControlType)(F_X | F_Y | F_D)},        //	4-5
    {4, 9, DIR_W, (DriveClass::TrackControlType)(F_X | F_Y | F_D)},         //	4-6
    {0, 0, DIR_NW, F_},                                                     //	4-7 !
    {0, 0, DIR_N, F_},                                                      //	5-0 !
    {0, 0, DIR_NE, F_},                                                     //	5-1 !
    {0, 0, DIR_E, F_},                                                      //	5-2 !
    {5, 10, DIR_SE, (DriveClass::TrackControlType)(F_T | F_D)},             //	5-3
    {6, 8, DIR_S, (DriveClass::TrackControlType)(F_T | F_D)},               //	5-4
    {2, 0, DIR_SW, F_T},                                                    //	5-5
    {6, 8, DIR_W, (DriveClass::TrackControlType)(F_X | F_Y | F_D)},         //	5-6
    {5, 10, DIR_NW, (DriveClass::TrackControlType)(F_X | F_Y | F_D)},       //	5-7
    {4, 9, DIR_N, (DriveClass::TrackControlType)(F_T | F_Y | F_D)},         //	6-0
    {0, 0, DIR_NE, F_},                                                     //	6-1 !
    {0, 0, DIR_E, F_},                                                      //	6-2 !
    {0, 0, DIR_SE, F_},                                                     //	6-3 !
    {4, 9, DIR_S, (DriveClass::TrackControlType)(F_T | F_D)},               //	6-4
    {3, 7, DIR_SW, (DriveClass::TrackControlType)(F_T | F_D)},              //	6-5
    {1, 0, DIR_W, F_T},                                                     //	6-6
    {3, 7, DIR_NW, (DriveClass::TrackControlType)(F_T | F_Y | F_D)},        //	6-7
    {6, 8, DIR_N, (DriveClass::TrackControlType)(F_T | F_Y | F_D)},         //	7-0
    {5, 10, DIR_NE, (DriveClass::TrackControlType)(F_T | F_Y | F_D)},       //	7-1
    {0, 0, DIR_E, F_},                                                      //	7-2 !
    {0, 0, DIR_SE, F_},                                                     //	7-3 !
    {0, 0, DIR_S, F_},                                                      //	7-4 !
    {5, 10, DIR_SW, (DriveClass::TrackControlType)(F_X | F_D)},             //	7-5
    {6, 8, DIR_W, (DriveClass::TrackControlType)(F_X | F_D)},               //	7-6
    {2, 0, DIR_NW, F_X},                                                    //	7-7

    {11, 11, DIR_SW, F_},    // Backup harvester into refinery.
    {12, 12, DIR_SW_X2, F_}, // Drive back into refinery.
    {13, 13, DIR_SW, F_}     // Drive out of weapons factory.
};