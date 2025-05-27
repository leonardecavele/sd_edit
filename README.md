## OBJ: mini console en C qui prend plusieurs instructions
éditer des .wav (uniquement),  
faire des trucs aléatoires,  

## FONCTIONNEMENT:

générer une array ensuite utilisables en presets:  

SEGMENT de PRESET = STRUCT  

```
typedef struct {  
    int name;        //de 1 à 2  
    int prmtr;      //intensité du pitch par exemple  
    int mix;        //% du mix  
    int length;     //en échantillons  
} Segment;  
  
typedef struct {
    int nb_seg;
    Segment* segments;
} Preset;
```

EFFETS:  
1 : PITCH  
2 : ?  

## COMMANDES:

do example.wav  
(charge sound.wav et génère un preset aléatoire, l'applique et sauvegarde le son créé)  

play example.wav  
(joue le fichier)  

save preset p1  
(sauvegarde le preset courant dans un fichier 'presets.txt')  

do example.wav p1  
(applique le preset 'p1' sur 'example.wav' et sauvegarde le son créé)  

preset 'format du preset'  
(créé un preset)  

//to do: trouver un moyen de mélanger les segments (et pourquoi)  
