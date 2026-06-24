with Ada.Containers;
with Ada.Containers.Indefinite_Hashed_Maps;
with Ada.Containers.Vectors;
with Ada.Strings.Hash;
with Ada.Text_IO;
with Ada.Unchecked_Deallocation;
with Wordcount_ASCII;
with Wordcount_Checksum;

package body Wordcount
  with SPARK_Mode => Off
is
   use type Count;

   package Count_IO is new Ada.Text_IO.Modular_IO (Count);
   package Natural_IO is new Ada.Text_IO.Integer_IO (Natural);

   package Word_Maps is new
     Ada.Containers.Indefinite_Hashed_Maps
       (Key_Type        => String,
        Element_Type    => Count,
        Hash            => Ada.Strings.Hash,
        Equivalent_Keys => "=");

   package Entry_Vectors is new
     Ada.Containers.Vectors
       (Index_Type   => Positive,
        Element_Type => Word_Entry);

   function Entry_Before
     (Left : Word_Entry; Right : Word_Entry) return Boolean;

   package Entry_Sorting is new
     Entry_Vectors.Generic_Sorting ("<" => Entry_Before);

   procedure Free is new
     Ada.Unchecked_Deallocation
       (Object => Entry_Array,
        Name   => Entry_Array_Access);

   procedure Commit_Word
     (Counts : in out Word_Maps.Map; Word : String; Total : in out Count);

   procedure Commit_Word
     (Counts : in out Word_Maps.Map; Word : String; Total : in out Count)
   is
      Cursor : constant Word_Maps.Cursor := Counts.Find (Word);
   begin
      if Word_Maps.Has_Element (Cursor) then
         Counts.Replace_Element
           (Cursor, Word_Maps.Element (Cursor) + Count'(1));
      else
         Counts.Insert (Word, 1);
      end if;

      Total := Total + Count'(1);
   end Commit_Word;

   function Count_Bytes
     (Bytes    : Ada.Streams.Stream_Element_Array;
      Top      : Positive;
      Max_Word : Natural) return Result
   is
      Counts        : Word_Maps.Map;
      Entries       : Entry_Vectors.Vector;
      Cursor        : Word_Maps.Cursor;
      Limit         : Natural;
      Max_Word_Size : constant Wordcount_ASCII.Word_Limit :=
        Wordcount_ASCII.Normalize_Max_Word (Max_Word);
      Output        : Result;
      Word          : String (1 .. Wordcount_ASCII.Max_Word);
      Word_Length   : Natural := 0;
   begin
      Counts.Reserve_Capacity (Ada.Containers.Count_Type (Bytes'Length / 32));

      for Byte of Bytes loop
         declare
            Value : constant Wordcount_ASCII.Byte :=
              Wordcount_ASCII.Byte (Byte);
         begin
            if Wordcount_ASCII.Is_Letter (Value) then
               if Word_Length < Max_Word_Size then
                  Word_Length := Word_Length + 1;
                  Word (Word_Length) := Wordcount_ASCII.Lower (Value);
               end if;
            elsif Word_Length > 0 then
               Commit_Word (Counts, Word (1 .. Word_Length), Output.Total);
               Word_Length := 0;
            end if;
         end;
      end loop;

      if Word_Length > 0 then
         Commit_Word (Counts, Word (1 .. Word_Length), Output.Total);
      end if;

      Output.Unique := Natural (Counts.Length);
      Entries.Reserve_Capacity (Counts.Length);
      Cursor := Counts.First;

      while Word_Maps.Has_Element (Cursor) loop
         Entries.Append
           (Word_Entry'
              (Word        =>
                 Ada.Strings.Unbounded.To_Unbounded_String
                   (Word_Maps.Key (Cursor)),
               Occurrences => Word_Maps.Element (Cursor)));
         Word_Maps.Next (Cursor);
      end loop;

      Entry_Sorting.Sort (Entries);
      Limit := Natural'Min (Top, Natural (Entries.Length));

      if Limit > 0 then
         Output.Top := new Entry_Array (1 .. Limit);

         for Index in 1 .. Limit loop
            Output.Top (Index) := Entries.Element (Index);
         end loop;
      end if;

      return Output;
   end Count_Bytes;

   function Entry_Before (Left : Word_Entry; Right : Word_Entry) return Boolean
   is
      Left_Word  : constant String :=
        Ada.Strings.Unbounded.To_String (Left.Word);
      Right_Word : constant String :=
        Ada.Strings.Unbounded.To_String (Right.Word);
   begin
      if Left.Occurrences /= Right.Occurrences then
         return Left.Occurrences > Right.Occurrences;
      end if;

      return Left_Word < Right_Word;
   end Entry_Before;

   function Checksum (Value : Result) return Interfaces.Unsigned_32 is
      Mixed : Interfaces.Unsigned_32 := Wordcount_Checksum.Offset;
   begin
      Mixed := Wordcount_Checksum.Mix_U64 (Mixed, Value.Total);
      Mixed :=
        Wordcount_Checksum.Mix_U64
          (Mixed, Wordcount_Checksum.Count (Value.Unique));

      if Value.Top /= null then
         for Item of Value.Top.all loop
            declare
               Word : constant String :=
                 Ada.Strings.Unbounded.To_String (Item.Word);
            begin
               for Character_Value of Word loop
                  Mixed :=
                    Wordcount_Checksum.Mix_Byte
                      (Mixed, Character'Pos (Character_Value));
               end loop;
            end;

            Mixed := Wordcount_Checksum.Mix_U64 (Mixed, Item.Occurrences);
         end loop;
      end if;

      return Mixed;
   end Checksum;

   procedure Put_Count (Value : Count);

   procedure Put_Natural (Value : Natural);

   procedure Put_Count (Value : Count) is
   begin
      Count_IO.Put (Value, Width => 0);
   end Put_Count;

   procedure Put_Natural (Value : Natural) is
   begin
      Natural_IO.Put (Value, Width => 0);
   end Put_Natural;

   procedure Render_JSON (Value : Result) is
      First : Boolean := True;
   begin
      Ada.Text_IO.Put ("{""total"":");
      Put_Count (Value.Total);
      Ada.Text_IO.Put (",""unique"":");
      Put_Natural (Value.Unique);
      Ada.Text_IO.Put (",""top"":[");

      if Value.Top /= null then
         for Item of Value.Top.all loop
            if First then
               First := False;
            else
               Ada.Text_IO.Put (",");
            end if;

            Ada.Text_IO.Put ("{""word"":""");
            Ada.Text_IO.Put (Ada.Strings.Unbounded.To_String (Item.Word));
            Ada.Text_IO.Put (""",""count"":");
            Put_Count (Item.Occurrences);
            Ada.Text_IO.Put ("}");
         end loop;
      end if;

      Ada.Text_IO.Put_Line ("]}");
   end Render_JSON;

   procedure Render_Text (Value : Result) is
   begin
      Ada.Text_IO.Put_Line ("count word");

      if Value.Top /= null then
         for Item of Value.Top.all loop
            Put_Count (Item.Occurrences);
            Ada.Text_IO.Put (" ");
            Ada.Text_IO.Put_Line (Ada.Strings.Unbounded.To_String (Item.Word));
         end loop;
      end if;

      Ada.Text_IO.Put ("total ");
      Put_Count (Value.Total);
      Ada.Text_IO.New_Line;
      Ada.Text_IO.Put ("unique ");
      Put_Natural (Value.Unique);
      Ada.Text_IO.New_Line;
   end Render_Text;

   procedure Release (Value : in out Result) is
   begin
      if Value.Top /= null then
         Free (Value.Top);
      end if;

      Value.Total := 0;
      Value.Unique := 0;
   end Release;
end Wordcount;
