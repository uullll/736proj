#
# A simple CloudLab profile
#
import geni.portal as portal
import geni.rspec.pg as rspec

request = portal.Context().makeRequestRSpec()

# request one VM
node = request.RawPC("node1")
node.disk_image = "urn:publicid:IDN+emulab.net+image+UBUNTU20-64-STD"

# print the RSpec to the enclosing page.
portal.Context().printRequestRSpec()
