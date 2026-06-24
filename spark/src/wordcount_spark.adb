with Ada.Command_Line;
with Ada.Exceptions;
with Ada.Real_Time;
with Ada.Streams;
with Ada.Streams.Stream_IO;
with Ada.Strings.Unbounded;
with Ada.Text_IO;
with Interfaces;
with Wordcount;
with Wordcount_Checksum;

procedure Wordcount_Spark with SPARK_Mode => Off is
   Usage : constant String :=
     "usage: wordcount_spark [--json] [--top N] [--max-word N] <file>";

   Usage_Error : exception;

   type Options is record
      Path           : Ada.Strings.Unbounded.Unbounded_String;
      Has_Path       : Boolean := False;
      Top            : Positive := 10;
      Max_Word       : Natural := 1_024;
      Bench_Runs     : Natural := 0;
      Bench_Runs_Set : Boolean := False;
      Bench_Warmups  : Natural := 0;
      JSON           : Boolean := False;
   end record;

   package Duration_IO is new Ada.Text_IO.Fixed_IO (Duration);
   package Checksum_IO is new Ada.Text_IO.Modular_IO (Interfaces.Unsigned_32);

   use type Ada.Real_Time.Time;
   use type Ada.Streams.Stream_IO.Count;

   function Starts_With (Value : String; Prefix : String) return Boolean;

   function Suffix_After (Value : String; Prefix : String) return String;

   function Parse_Natural (Value : String; Name : String) return Natural;

   function Next_Argument (Index : in out Positive) return String;

   procedure Set_Top (State : in out Options; Value : String);

   procedure Set_Bench_Runs (State : in out Options; Value : String);

   function Parse_Options return Options;

   function Read_File (Path : String) return Ada.Streams.Stream_Element_Array;

   function Count_Checksum
     (Bytes : Ada.Streams.Stream_Element_Array; State : Options)
      return Interfaces.Unsigned_32;

   procedure Render_Bench
     (Bytes : Ada.Streams.Stream_Element_Array; State : Options);

   function Starts_With (Value : String; Prefix : String) return Boolean is
   begin
      return
        Value'Length >= Prefix'Length
        and then Value (Value'First .. Value'First + Prefix'Length - 1)
                 = Prefix;
   end Starts_With;

   function Suffix_After (Value : String; Prefix : String) return String is
   begin
      return Value (Value'First + Prefix'Length .. Value'Last);
   end Suffix_After;

   function Parse_Natural (Value : String; Name : String) return Natural is
      Parsed : Natural := 0;
   begin
      if Value'Length = 0 then
         raise Usage_Error
           with "wordcount_spark: " & Name & " must be a number";
      end if;

      for Digit of Value loop
         if Digit not in '0' .. '9' then
            raise Usage_Error
              with "wordcount_spark: " & Name & " must be a number";
         end if;

         if Parsed
           > (Natural'Last - (Character'Pos (Digit) - Character'Pos ('0')))
             / 10
         then
            raise Usage_Error
              with "wordcount_spark: " & Name & " is too large";
         end if;

         Parsed := Parsed * 10 + (Character'Pos (Digit) - Character'Pos ('0'));
      end loop;

      return Parsed;
   end Parse_Natural;

   function Next_Argument (Index : in out Positive) return String is
   begin
      Index := Index + 1;

      if Index > Ada.Command_Line.Argument_Count then
         raise Usage_Error with Usage;
      end if;

      return Ada.Command_Line.Argument (Index);
   end Next_Argument;

   procedure Set_Top (State : in out Options; Value : String) is
      Parsed : constant Natural := Parse_Natural (Value, "--top");
   begin
      if Parsed = 0 then
         raise Usage_Error with Usage;
      end if;

      State.Top := Parsed;
   end Set_Top;

   procedure Set_Bench_Runs (State : in out Options; Value : String) is
   begin
      State.Bench_Runs := Parse_Natural (Value, "--bench-runs");
      State.Bench_Runs_Set := True;
   end Set_Bench_Runs;

   function Parse_Options return Options is
      State : Options;
      Index : Positive := 1;
   begin
      while Index <= Ada.Command_Line.Argument_Count loop
         declare
            Argument : constant String := Ada.Command_Line.Argument (Index);
         begin
            if Argument = "--json" then
               State.JSON := True;
            elsif Argument = "--top" then
               Set_Top (State, Next_Argument (Index));
            elsif Starts_With (Argument, "--top=") then
               Set_Top (State, Suffix_After (Argument, "--top="));
            elsif Argument = "--max-word" then
               State.Max_Word :=
                 Parse_Natural (Next_Argument (Index), "--max-word");
            elsif Starts_With (Argument, "--max-word=") then
               State.Max_Word :=
                 Parse_Natural
                   (Suffix_After (Argument, "--max-word="), "--max-word");
            elsif Argument = "--bench-runs" then
               Set_Bench_Runs (State, Next_Argument (Index));
            elsif Starts_With (Argument, "--bench-runs=") then
               Set_Bench_Runs
                 (State, Suffix_After (Argument, "--bench-runs="));
            elsif Argument = "--bench-warmups" then
               State.Bench_Warmups :=
                 Parse_Natural (Next_Argument (Index), "--bench-warmups");
            elsif Starts_With (Argument, "--bench-warmups=") then
               State.Bench_Warmups :=
                 Parse_Natural
                   (Suffix_After (Argument, "--bench-warmups="),
                    "--bench-warmups");
            elsif Starts_With (Argument, "-") then
               raise Usage_Error with Usage;
            elsif not State.Has_Path then
               State.Path :=
                 Ada.Strings.Unbounded.To_Unbounded_String (Argument);
               State.Has_Path := True;
            else
               raise Usage_Error with Usage;
            end if;
         end;

         Index := Index + 1;
      end loop;

      if not State.Has_Path or (State.Bench_Runs_Set and State.Bench_Runs = 0)
      then
         raise Usage_Error with Usage;
      end if;

      return State;
   end Parse_Options;

   function Read_File (Path : String) return Ada.Streams.Stream_Element_Array
   is
      File : Ada.Streams.Stream_IO.File_Type;
   begin
      Ada.Streams.Stream_IO.Open
        (File => File, Mode => Ada.Streams.Stream_IO.In_File, Name => Path);

      declare
         Length : constant Ada.Streams.Stream_IO.Count :=
           Ada.Streams.Stream_IO.Size (File);
         Bytes  :
           Ada.Streams.Stream_Element_Array
             (1 .. Ada.Streams.Stream_Element_Offset (Length));
         Last   : Ada.Streams.Stream_Element_Offset;
      begin
         if Length > 0 then
            Ada.Streams.Stream_IO.Read (File, Bytes, Last);
         end if;

         Ada.Streams.Stream_IO.Close (File);
         return Bytes;
      end;
   exception
      when others =>
         if Ada.Streams.Stream_IO.Is_Open (File) then
            Ada.Streams.Stream_IO.Close (File);
         end if;

         raise;
   end Read_File;

   function Count_Checksum
     (Bytes : Ada.Streams.Stream_Element_Array; State : Options)
      return Interfaces.Unsigned_32
   is
      Result : Wordcount.Result :=
        Wordcount.Count_Bytes (Bytes, State.Top, State.Max_Word);
      Value  : constant Interfaces.Unsigned_32 := Wordcount.Checksum (Result);
   begin
      Wordcount.Release (Result);
      return Value;
   end Count_Checksum;

   procedure Render_Bench
     (Bytes : Ada.Streams.Stream_Element_Array; State : Options)
   is
      Checksum : Interfaces.Unsigned_32 := Wordcount_Checksum.Offset;
      Finished : Ada.Real_Time.Time;
      Started  : Ada.Real_Time.Time;
      Mean_MS  : Duration;
   begin
      for Warmup in 1 .. State.Bench_Warmups loop
         Checksum := Count_Checksum (Bytes, State);
      end loop;

      Checksum := Wordcount_Checksum.Offset;
      Started := Ada.Real_Time.Clock;

      for Run in 1 .. State.Bench_Runs loop
         Checksum :=
           Wordcount_Checksum.Mix_U32
             (Checksum, Count_Checksum (Bytes, State));
      end loop;

      Finished := Ada.Real_Time.Clock;
      Mean_MS :=
        (Ada.Real_Time.To_Duration (Finished - Started) * 1_000)
        / State.Bench_Runs;

      Ada.Text_IO.Put ("{""mean_ms"":");
      Duration_IO.Put (Mean_MS, Fore => 1, Aft => 6, Exp => 0);
      Ada.Text_IO.Put (",""checksum"":");
      Checksum_IO.Put (Checksum, Width => 0);
      Ada.Text_IO.Put_Line ("}");
   end Render_Bench;

   State : constant Options := Parse_Options;
   Bytes : constant Ada.Streams.Stream_Element_Array :=
     Read_File (Ada.Strings.Unbounded.To_String (State.Path));
begin
   if State.Bench_Runs > 0 then
      Render_Bench (Bytes, State);
   else
      declare
         Result : Wordcount.Result :=
           Wordcount.Count_Bytes (Bytes, State.Top, State.Max_Word);
      begin
         if State.JSON then
            Wordcount.Render_JSON (Result);
         else
            Wordcount.Render_Text (Result);
         end if;

         Wordcount.Release (Result);
      end;
   end if;
exception
   when Error : Usage_Error =>
      Ada.Text_IO.Put_Line
        (Ada.Text_IO.Standard_Error, Ada.Exceptions.Exception_Message (Error));
      Ada.Command_Line.Set_Exit_Status (Ada.Command_Line.Failure);
   when Error : others =>
      Ada.Text_IO.Put_Line
        (Ada.Text_IO.Standard_Error,
         "wordcount_spark: " & Ada.Exceptions.Exception_Message (Error));
      Ada.Command_Line.Set_Exit_Status (Ada.Command_Line.Failure);
end Wordcount_Spark;
