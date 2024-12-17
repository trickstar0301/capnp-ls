# Copyright (c) 2024 Atsushi Tomida
# 
# Licensed under the MIT License.
# See LICENSE file in the project root for full license information.

@0xb4f2917cd520da89;

using Common = import "/common.capnp";

struct Map(Key, Value) {
  entries @0 :List(Entry);
                       
  struct Entry {
    key @0 :Key;
    value @1 :Value;
  }
}

struct People {
  byName @0 :Map(Text, Person);
  # Maps names to Person instances.
}


struct Thing {
  thingName @0 :Map(Text, Text);
  # Maps names to Person instances.
}

struct Person {
  id @0 :UInt32;
  job @1 :Common.JobType;
#  job2 @2 :import "/common.capnp".JobType;
}

