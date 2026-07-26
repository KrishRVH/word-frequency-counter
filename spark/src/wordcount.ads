with Ada.Containers.Vectors;
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

   package Entry_Vectors is new
     Ada.Containers.Vectors
       (Index_Type   => Positive,
        Element_Type => Word_Entry);

   type Result is record
      Total  : Count := 0;
      Unique : Natural := 0;
      Top    : Entry_Vectors.Vector;
   end record;

   function Count_Bytes
     (Bytes    : Ada.Streams.Stream_Element_Array;
      Top      : Positive;
      Max_Word : Natural) return Result;

   function Checksum (Value : Result) return Interfaces.Unsigned_32;

   procedure Render_JSON (Value : Result);
   procedure Render_Text (Value : Result);
end Wordcount;
