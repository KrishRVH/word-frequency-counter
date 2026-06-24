with Ada.Streams;
with Ada.Strings.Unbounded;
with Interfaces;

package Wordcount
  with SPARK_Mode => Off
is
   subtype Count is Interfaces.Unsigned_64;

   type Word_Entry is record
      Word        : Ada.Strings.Unbounded.Unbounded_String;
      Occurrences : Count;
   end record;

   type Entry_Array is array (Positive range <>) of Word_Entry;
   type Entry_Array_Access is access Entry_Array;

   type Result is record
      Total  : Count := 0;
      Unique : Natural := 0;
      Top    : Entry_Array_Access := null;
   end record;

   function Count_Bytes
     (Bytes    : Ada.Streams.Stream_Element_Array;
      Top      : Positive;
      Max_Word : Natural) return Result;

   function Checksum (Value : Result) return Interfaces.Unsigned_32;

   procedure Render_JSON (Value : Result);
   procedure Render_Text (Value : Result);
   procedure Release (Value : in out Result);
end Wordcount;
