# Copyright (c) 2024 Atsushi Tomida
# 
# Licensed under the MIT License.
# See LICENSE file in the project root for full license information.

@0x97d4012a9e8bb0cb;

using Common = import "/common.capnp";

const myCompanyId :UInt3 = 123;

interface EmployeeManagement {
  addEmployee @0 (employee :Employe) -> (id :Int32);
  updateEmployee @1 (id :Int32, employee :Employee) -> (employee :Employee);
  listEmployees @2 () -> (employees :List(Employee));
  struct Employee {
    name @0 :Text;
    age @1 :UInt32;
    job @2 :Common.JobTyp;
    job2 @3 :import "/common.capnp".JobType;
    bar @4 :Foo.Bar.Baz;
  }
}

struct MyStruct {
  field @0 :Foo.Bar.Baz;
}

struct Foo {
  struct Bar {
    value @0 :Int32;
    struct Baz {
      value @0 :Int32;
    }
  }
}