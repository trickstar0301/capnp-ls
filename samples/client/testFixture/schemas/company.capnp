# Copyright (c) 2024 Atsushi Tomida
# 
# Licensed under the MIT License.
# See LICENSE file in the project root for full license information.

@0xe6bd36f8b8744aef;

using Common = import "/common.capnp";

const myCompanyId :UInt32 = 123;

interface EmployeeManagement {
  addEmployee @0 (employee :Employee) -> (id :Int32);
  updateEmployee @1 (id :Int32, employee :Employee) -> (employee :Employee);
  listEmployees @2 () -> (employees :List(Employee));
  struct Employee {
    name @0 :Text;
    age @1 :UInt32;
    job @2 :Common.JobType;
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