drop table if exists as_token_test;
create table as_token_test (resource jsonb);

insert into as_token_test (resource) values (
$$
{
  "resourceType": "Patient",
  "id": "example",
  "identifier": [{
    "use": "usual",
    "type": { "coding": [{"system": "http://hl7.org/fhir/v2/0203", "code": "MR"}] },
    "system": "urn:oid:1.2.36.146.595.217.0.1",
    "value": "12345",
    "period": {"start": "2001-05-06"},
    "assigner": {"display": "Acme Healthcare"}
  }]
}
$$
);

SELECT fhirpath_as_token(resource, '.identifier', 'Identifier') from as_token_test;
