# New Dawn base material library (ND-016)

CC0 PBR materials from ambientCG (https://ambientcg.com, licence https://docs.ambientcg.com/license/, "CC0 1.0 Universal"), re-encoded by
`tools/newdawn/matlib/build_library.py` to 1024x1024 JPEG (albedo, normal GL, roughness, height; AO and the 2K originals are not shipped).
Used only as input to the first-run texture generator (`mm/2s2h/NewDawn/RuntimeTextures.cpp`): the player never sees them unmodified.
Total: 57.4 MB for 35 sets. Add or swap a set by editing `library.json` and rerunning the tool.

| id | class | roles | region | asset | page | zip sha256 |
|---|---|---|---|---|---|---|
| rock050 | stone | face, rock | any | Rock050 | https://ambientcg.com/view?id=Rock050 | `1f8990cc125e881f...` |
| rock060 | stone | face, rock | any | Rock060 | https://ambientcg.com/view?id=Rock060 | `d130529dd343fd12...` |
| rock030 | rock | rock | any | Rock030 | https://ambientcg.com/view?id=Rock030 | `18992ddc09f8f0cc...` |
| rock031 | rock | rock | any | Rock031 | https://ambientcg.com/view?id=Rock031 | `694980144b3b5bf2...` |
| rock061 | rock | rock, strata | any | Rock061 | https://ambientcg.com/view?id=Rock061 | `34d15ee39ed72be0...` |
| rock051 | rock | rock, strata | any | Rock051 | https://ambientcg.com/view?id=Rock051 | `3494755ee283b120...` |
| rock058 | stone | face, rock | any | Rock058 | https://ambientcg.com/view?id=Rock058 | `4b6dc8c6a5314957...` |
| rock062 | stone | face, rock | any | Rock062 | https://ambientcg.com/view?id=Rock062 | `7f6290a24eb86b68...` |
| rocks011 | rock | rock, strata | any | Rocks011 | https://ambientcg.com/view?id=Rocks011 | `728cc47be62f2a22...` |
| concrete034 | mortar | mortar, face | any | Concrete034 | https://ambientcg.com/view?id=Concrete034 | `fafd7cd3aee2b703...` |
| ground085 | mortar | mortar | any | Ground085 | https://ambientcg.com/view?id=Ground085 | `d77024923ecb39ff...` |
| gravel040 | mortar | mortar, ground | any | Gravel040 | https://ambientcg.com/view?id=Gravel040 | `7094774f897dd0fd...` |
| plaster006 | plaster | plaster | any | Plaster006 | https://ambientcg.com/view?id=Plaster006 | `8e9cec359f0c4eab...` |
| plaster002 | plaster | plaster | any | Plaster002 | https://ambientcg.com/view?id=Plaster002 | `c364e72a39ea894c...` |
| paintedplaster016 | plaster | plaster | any | PaintedPlaster016 | https://ambientcg.com/view?id=PaintedPlaster016 | `000f96231abe04b4...` |
| planks033b | wood | planks | any | Planks033B | https://ambientcg.com/view?id=Planks033B | `d27ff4d344760227...` |
| planks037a | wood | planks | any | Planks037A | https://ambientcg.com/view?id=Planks037A | `10b25dd182a8b1bd...` |
| woodfloor041 | wood | planks | any | WoodFloor041 | https://ambientcg.com/view?id=WoodFloor041 | `f80d5875cfd72a26...` |
| bark012 | wood | bark | any | Bark012 | https://ambientcg.com/view?id=Bark012 | `8514e883c1171f64...` |
| roofingtiles012a | roof | tiles | any | RoofingTiles012A | https://ambientcg.com/view?id=RoofingTiles012A | `71cf14afa940d20e...` |
| roofingtiles003 | roof | tiles | any | RoofingTiles003 | https://ambientcg.com/view?id=RoofingTiles003 | `c57e253c04f112a2...` |
| thatchedroof001a | thatch | thatch | any | ThatchedRoof001A | https://ambientcg.com/view?id=ThatchedRoof001A | `05631e8dcf50fc69...` |
| ground079s | dirt | ground | any | Ground079S | https://ambientcg.com/view?id=Ground079S | `6d7f7f6c4582bc96...` |
| ground048 | dirt | ground | any | Ground048 | https://ambientcg.com/view?id=Ground048 | `8219d18b030f1b93...` |
| ground109 | dirt | ground | any | Ground109 | https://ambientcg.com/view?id=Ground109 | `1f32c5ffa599e32c...` |
| ground054 | sand | ground | any | Ground054 | https://ambientcg.com/view?id=Ground054 | `643e7c8689bac041...` |
| ground097 | sand | ground, ripples | any | Ground097 | https://ambientcg.com/view?id=Ground097 | `edf4edcc95ef82b2...` |
| grass004 | grass | ground | any | Grass004 | https://ambientcg.com/view?id=Grass004 | `d7d3f08e8bb1e59a...` |
| grass002 | grass | ground | any | Grass002 | https://ambientcg.com/view?id=Grass002 | `ee62c9fa9cb5368a...` |
| moss002 | moss | moss | any | Moss002 | https://ambientcg.com/view?id=Moss002 | `03fe8868a5b5618b...` |
| metal055a | metal | metal | any | Metal055A | https://ambientcg.com/view?id=Metal055A | `f3d4d3e7b5fbe47e...` |
| rust007 | metal | metal, rust | any | Rust007 | https://ambientcg.com/view?id=Rust007 | `51112d590d495901...` |
| fabric066 | fabric | fabric | any | Fabric066 | https://ambientcg.com/view?id=Fabric066 | `7af520fce64a91a7...` |
| snow007a | snow | snow | any | Snow007A | https://ambientcg.com/view?id=Snow007A | `b1985eec6f3e3997...` |
| ice001 | ice | ice | any | Ice001 | https://ambientcg.com/view?id=Ice001 | `8b947252e01f8d45...` |

Licence evidence: the ambientCG licence page (fetched by the tool; the build aborts if it stops saying "CC0 1.0 Universal"). ambientCG's API has no per-asset licence field.
